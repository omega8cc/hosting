<?php
/**
 * @file
 * Implement drush hooks for the hosting migrate module.
 */

function drush_hosting_migrate_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if ($task->ref->type == 'site' && $task->task_type == 'migrate') {
    $platform = node_load($task->task_args['target_platform']);
    $task->args[2] = hosting_context_name($platform->nid);

    $site = $task->ref;

    if (hosting_site_get_domain($site->title) != hosting_site_get_domain($task->task_args['new_uri'])) {
      $task->args[3] = '@' . hosting_site_get_domain($task->task_args['new_uri']);
    }

    if ($site->db_server != $task->task_args['new_db_server']) {
      $task->options['new_db_server'] = hosting_context_name($task->task_args['new_db_server']);
    }


    $profile_instance = _hosting_migrate_get_target_profile_name($task);
    if (is_object($profile_instance) && isset($profile_instance->short_name)) {
      $task->options['profile'] = $profile_instance->short_name;
    }
    else {
      drush_set_error("HOSTING_NO_VALID_PROFILE", _hosting_migrate_no_valid_profile_message($task, $platform));
    }
  }
}

/**
 * Explain a missing install profile on the migrate/clone target platform.
 *
 * The error this replaces ("There are no valid install profiles on the
 * target platform to migrate to") named neither the profile that is missing
 * nor the one action that fixes it, so an operator reading the task log had
 * nothing to act on. The usual cause is a target platform whose packages
 * were never registered -- a platform that has not been verified since it
 * was created, or whose verify failed -- and the profile the site needs is
 * simply absent from the package list.
 */
function _hosting_migrate_no_valid_profile_message($task, $platform = NULL) {
  $profile_name = '';
  if (!empty($task->ref->profile)) {
    $profile = node_load($task->ref->profile);
    if (is_object($profile)) {
      if (!empty($profile->short_name)) {
        $profile_name = $profile->short_name;
      }
      elseif (!empty($profile->title)) {
        $profile_name = $profile->title;
      }
    }
  }
  if ($profile_name === '') {
    $profile_name = dt('required');
  }

  $platform_name = '';
  if (is_object($platform) && !empty($platform->title)) {
    $platform_name = $platform->title;
  }
  elseif (!empty($task->task_args['target_platform'])) {
    $target = node_load($task->task_args['target_platform']);
    if (is_object($target) && !empty($target->title)) {
      $platform_name = $target->title;
    }
  }
  if ($platform_name === '') {
    $platform_name = dt('selected as the target');
  }

  return dt('The @profile install profile is not registered on the target platform @platform; run a Verify task on that platform first.', array(
    '@profile' => $profile_name,
    '@platform' => $platform_name,
  ));
}

function _hosting_migrate_get_target_profile_name($task) {
  // load the original install profile
  $profile = node_load($task->ref->profile);

  // get the profile instance we are migrating to.
  $profile_instance = hosting_package_instance_load(array(
    'i.rid' => $task->task_args['target_platform'],
    'p.short_name' => $profile->short_name,
  ));

  if (empty($profile_instance)) {
    // get a possible upgrade path for the profile.
    $profile_instance = hosting_package_instance_load(array(
      'i.rid' => $task->task_args['target_platform'],
      'p.old_short_name' => $profile->short_name,
    ));
  }

  return $profile_instance;

}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_migrate_post_hosting_migrate_task($task, $data) {
  if ($task->ref->type == 'site') {
    $target = $task->task_args['target_platform'];
    $site = $task->ref;

    if (hosting_site_get_domain($site->title) != hosting_site_get_domain($task->task_args['new_uri'])) {
      $site->title = hosting_site_clean_domain($task->task_args['new_uri']);
      hosting_context_register($site->nid, hosting_site_get_domain($site->title));

      // Auto-disable Encryption when the site's main machine-name changes.
      // SSL keys/certificates are bound to the machine-name and are invalid for
      // the new name; leaving encryption enabled would make the renamed site
      // reuse the old name's certificate on the verify queued below. The
      // operator re-enables encryption for the new domain deliberately. This is
      // the encryption analogue of the cron safety behaviour.
      if (defined('HOSTING_SSL_DISABLED') && !empty($site->ssl_enabled)
          && !(function_exists('hosting_get_hostmaster_nid') && $site->nid == hosting_get_hostmaster_nid())) {
        $site->ssl_enabled = HOSTING_SSL_DISABLED;
        $site->ssl_key = 0;
        drush_log(dt('[migrate] Auto-disabled Encryption on renamed site @uri', array('@uri' => $site->title)), 'info');
      }
    }

    if ($site->db_server != $task->task_args['new_db_server']) {
      $site->db_server = $task->task_args['new_db_server'];
    }
    $site->db_name = $data['context']['db_name'];

    $profile = hosting_package_instance_load(array(
      'i.rid' => $target,
      'p.short_name' => $data['context']['profile'],
    ));
    if ($profile) {
      $site->profile = $profile->package_id;
    }

    $site->verified = REQUEST_TIME;
    $site->no_verify = TRUE;
    $site->platform = $target;
    node_save($site);

    $task->ref = $site;

    //do the package synching
    $context = $data['context'];
    $packages = $context['packages'];
    hosting_package_sync($packages);
    hosting_package_instance_sync($task->ref->nid, $task->ref->type, $packages);

    // Record the backup created during migrate.
    $task->task_args['description'] = t('Pre-migration backup');
    hosting_site_post_hosting_backup_task($task, $data);

    // Run an extra verify task to update the vhost
    hosting_add_task($site->nid, 'verify');
  }
}
