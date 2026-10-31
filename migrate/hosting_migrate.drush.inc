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
    _hosting_migrate_record_on_target($task, $data);
  }
}

/**
 * Implements hook_hosting_TASK_TYPE_task_rollback().
 *
 * A migrate whose post phase failed after the source was retired finishes
 * forward in the backend: the site stays on its target platform
 * (provision_migrate_failure_outcome()), and its result says so in
 * migrate_kept_on_target. A backend that died there instead (a PHP fatal
 * runs no rollback; its result says DRUSH_NOT_COMPLETED) leaves the site's
 * alias and vhost on the target as well, and its result still carries
 * migrate_source_retiring and migrate_target_deployed. Either way the node
 * follows the site the way a completed migrate's does, so a Verify cannot
 * pull the record back onto a platform where the site is gone; the task
 * stays failed. Nothing changes otherwise: a backend that failed normally
 * and reports no kept flag rolled the site back itself, and a kept flag that
 * names another platform is reported and left.
 */
function hosting_migrate_hosting_migrate_task_rollback($task, $data) {
  if ($task->ref->type != 'site' || !is_array($data)
    || !isset($data['context']) || !is_array($data['context'])) {
    return;
  }
  $context = $data['context'];
  $target = hosting_context_name($task->task_args['target_platform']);
  if (!empty($context['migrate_kept_on_target'])) {
    if ($context['migrate_kept_on_target'] !== $target) {
      drush_log(dt('The backend kept the site on @kept, not on this task\'s target @target: the site record is left as it is.', array('@kept' => $context['migrate_kept_on_target'], '@target' => (string) $target)), 'warning');
      return;
    }
    drush_log(dt('The migrate failed after the source was retired: the site stays on @target, and its record follows it.', array('@target' => $target)), 'warning');
  }
  elseif ($target && !empty($context['migrate_source_retiring']) && !empty($context['migrate_target_deployed'])
    && isset($data['error_log']['DRUSH_NOT_COMPLETED'])) {
    drush_log(dt('The migrate stopped after it began to retire the source, before its own rollback: the site stays on @target, and its record follows it. What is left of the source is not cleaned up.', array('@target' => $target)), 'warning');
    // A backend that died before it reloaded the site from the copy still
    // reports the source's database: record the one the deploy reported. The
    // Verify queued below reads the rest from the site itself.
    if (isset($context['migrate_target_copy']['db_name']) && is_string($context['migrate_target_copy']['db_name'])
      && preg_match('/^[A-Za-z0-9_]+$/', $context['migrate_target_copy']['db_name'])) {
      $data['context']['db_name'] = $context['migrate_target_copy']['db_name'];
    }
  }
  else {
    return;
  }
  _hosting_migrate_record_on_target($task, $data);
}

/**
 * Record a migrated site on its target platform.
 *
 * The site's name, database, profile, platform and packages from the
 * backend's result, the migration package as a Pre-migration backup, and a
 * Verify to rewrite the vhost. Used by the post hook of a completed migrate
 * and by the rollback of one the backend finished forward.
 */
function _hosting_migrate_record_on_target($task, $data) {
  if ($task->ref->type == 'site') {
    $target = $task->task_args['target_platform'];
    $site = $task->ref;

    if (hosting_site_get_domain($site->title) != hosting_site_get_domain($task->task_args['new_uri'])) {
      $old_title = $site->title;
      $site->title = hosting_site_clean_domain($task->task_args['new_uri']);
      // The backend has renamed the site already: a live holder of the new
      // name is reported, and the record still takes the new title.
      hosting_context_register($site->nid, hosting_site_get_domain($site->title), TRUE);

      // Aliases redirected to the site's own name follow it to the new
      // name, and those redirected to its www or bare companion follow it to
      // the new name's matching one, as the backend moves that alias
      // (platform/migrate.provision.inc), so a canonical www stays www. Kept
      // on the old name, the redirection would name neither the site nor one
      // of its aliases, a target no site directory answers.
      $raw_old = preg_replace('/^www\./', '', $old_title);
      $raw_new = preg_replace('/^www\./', '', $site->title);
      if (!empty($site->redirection)) {
        $redir = hosting_site_clean_domain($site->redirection);
        if ($redir == $old_title) {
          $site->redirection = $site->title;
        }
        elseif ($redir == 'www.' . $raw_old) {
          $site->redirection = 'www.' . $raw_new;
        }
        elseif ($redir == $raw_old) {
          $site->redirection = $raw_new;
        }
      }

      // A custom alias that is the old name's www or bare companion moves to
      // the new name's, as the backend moves it: kept on the old name, the
      // Verify queued below would give the vhost the old companion back and
      // drop the new one, and a redirection moved to it above would name no
      // alias.
      if (!empty($site->aliases) && is_array($site->aliases)) {
        $old_companion = preg_match('/^www\./', $old_title) ? $raw_old : 'www.' . $raw_old;
        $new_companion = preg_match('/^www\./', $site->title) ? $raw_new : 'www.' . $raw_new;
        foreach ($site->aliases as $key => $alias) {
          if (hosting_site_clean_domain($alias) == $old_companion) {
            $site->aliases[$key] = $new_companion;
          }
        }
        $site->aliases = array_values(array_unique($site->aliases));
      }

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
