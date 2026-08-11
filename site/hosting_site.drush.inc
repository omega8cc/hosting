<?php

/**
 * @file
 * Drush hooks for the Hosting site module.
 */

/**
 * Implements hook_hosting_TASK_OBJECT_context_options().
 */
function hosting_hosting_site_context_options(&$task) {
  $task->context_options['db_server'] = hosting_context_name($task->ref->db_server);
  $task->context_options['platform'] = hosting_context_name($task->ref->platform);

  $task->context_options['uri'] = hosting_site_get_domain($task->ref->title);
  $task->context_options['language'] = $task->ref->site_language;

  $profile = node_load($task->ref->profile);
  if (!empty($profile->short_name)) {
    $task->context_options['profile'] = $profile->short_name;
  }

  $client = node_load($task->ref->client);
  $user = user_load($task->uid);
  if ($task->task_type != 'import' && $task->task_type != 'delete' && $task->task_type != 'verify') {
    $task->options['client_email'] = $user->mail;
  }
  $task->context_options['client_name'] = $client->uname;

  // Set "install_method" property, only if there is one present on the site node.
  // "profile" is used by default, we don't have to set it here.
  if (isset($task->ref->install_method) && !empty($task->ref->install_method)) {
    $task->context_options['install_method'] = $task->ref->install_method;
  }
}

/**
 * Implements hook_drush_context_import().
 */
function hosting_site_drush_context_import($context, &$node) {
  if ($context->type == 'site') {
    if (!isset($node->title)) {
      $node->title = hosting_site_get_domain($context->uri); // force lowercase for existing uri
    }
    $node->site_language = $context->language;
    $node->cron_key = $context->cron_key;
    $node->db_server = hosting_context_nid($context->db_server->name);
    $node->platform = hosting_context_nid($context->platform->name);

    // TODO: abstract this to remove duplication with import post hooks.
    $profile = hosting_package_instance_load(array(
      'i.rid' => $node->platform,
      'p.short_name' => $context->profile,
    ));

    if (!$profile) {
      // For D6 to D7 upgrades, try changing default to standard.
      if (!empty($context->profile) && $context->profile == 'default') {
        $profile = hosting_package_instance_load(array(
          'i.rid' => $node->platform,
          'p.short_name' => 'standard',
        ));
        if ($profile) {
           drush_log(dt("Could not find the Profile !profile for Platfrom ID !platform, substituted standard", array('!profile' => $context->profile, '!platform' => $node->platform)), 'warning');
        }
      }
    }

    // Log an error if there is still no profile.
    if (!$profile) {
      drush_set_error("HOSTING_SITE_IMPORT_ERROR", dt("Could not find the Profile !profile for Platfrom ID !platform", array('!profile' => $context->profile, '!platform' => $node->platform)));
    }
    $node->profile = $profile->package_id;

    $client = node_load(HOSTING_DEFAULT_CLIENT);
    if ($context->client_name) {
      $client = hosting_import_client($context->client_name);
    }
    $node->client = $client->nid;

    // An imported site the backend already reports as installed + enabled should
    // land ENABLED, not the default QUEUED. QUEUED reads as "awaiting install"
    // in the UI and hides the clone/backup/migrate task actions, so a perfectly
    // good imported site looks unusable until manually enabled. Only promote a
    // site the context confirms is enabled; leave anything else at its default.
    if (!empty($context->site_enabled) || drush_get_option('installed')) {
      $node->site_status = HOSTING_SITE_ENABLED;
    }
  }

}

/**
 * Map values of site node into command line arguments
 */
function drush_hosting_site_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');

  if ($task->task_type == 'restore') {
    $backup = hosting_site_get_backup($task->task_args['bid']);
    $task->args[1] = $backup['filename'];
  }

  if ($task->task_type == 'backup-delete') {
    foreach ($task->task_args as $bid => $filename) {
      if ($filename !== '0') {
        $backups[] = $filename;
      }
    }
    $task->args[1] = implode(',', $backups);
  }

  if ($task->task_type == 'leftover_purge') {
    _hosting_site_pre_leftover_purge_task($task);
  }

}

/**
 * Populate the backend invocation for the leftover_purge task (D-035).
 *
 * The pseudo-site node has NO backend context (nothing was ever installed
 * under its name), so the invocation is re-targeted at its PLATFORM context:
 * $task->ref->hosting_name is what drush_hosting_task() reads as the invoke
 * alias. Args carry the leftover directory name; options carry the recorded
 * db name + host (the backend drops the db only on a fresh re-parse match)
 * and the db server context to drop it on.
 *
 * Authoritative frontend collision gate: when ANY live (non-deleted) site
 * record fleet-wide carries the recorded db name, the drop is withheld
 * entirely (directory-only purge). This covers the cross-platform shape the
 * single-platform backend sweep cannot see — e.g. a site migrated away whose
 * stranded artifact still names a db that a live site now legitimately owns
 * on another platform/server.
 */
function _hosting_site_pre_leftover_purge_task($task) {
  if ($task->ref->type != 'site') {
    drush_set_error('HOSTING_LEFTOVER_REF',
      dt('The leftover_purge task runs on leftover pseudo-site nodes only.'));
    return;
  }
  $info = hosting_site_leftover_info($task->ref->nid);
  if (empty($info['leftover']) || empty($info['platform'])
    || !hosting_leftover_name_is_manageable($info['leftover'])) {
    drush_set_error('HOSTING_LEFTOVER_META',
      dt('This site node carries no leftover pseudo-site record; refusing to purge.'));
    return;
  }
  $platform = node_load($info['platform']);
  if (!is_object($platform) || empty($platform->hosting_name)
    || (isset($platform->platform_status) && $platform->platform_status == HOSTING_PLATFORM_DELETED)) {
    drush_set_error('HOSTING_LEFTOVER_PLATFORM',
      dt('The platform holding this leftover could not be loaded (deleted?).'));
    return;
  }
  // Re-target the backend invocation at the platform context.
  $task->ref->hosting_name = $platform->hosting_name;
  $task->args = array($info['leftover']);
  if (!empty($info['db_name'])) {
    // Fleet-wide authoritative check: a live site record owning this db name
    // anywhere means it is NOT an orphan — withhold the drop.
    $holder = db_query("SELECT n.title FROM {hosting_site} h INNER JOIN {node} n ON n.nid = h.nid
      WHERE h.db_name = :db AND h.status <> :deleted AND h.nid <> :self",
      array(':db' => $info['db_name'], ':deleted' => HOSTING_SITE_DELETED, ':self' => $task->ref->nid))->fetchField();
    if ($holder) {
      drush_log(dt('Database @db is recorded for live site @site — not an orphan; the purge will remove the directory only.',
        array('@db' => $info['db_name'], '@site' => $holder)), 'warning');
    }
    else {
      $task->options['leftover_db_name'] = $info['db_name'];
      if (!empty($info['db_host'])) {
        $task->options['leftover_db_host'] = $info['db_host'];
      }
      $db_server = hosting_context_name($task->ref->db_server);
      if (is_string($db_server) && $db_server !== '') {
        $task->options['leftover_db_server'] = ltrim($db_server, '@');
      }
    }
  }
}


/**
 * implementation of the hosting_post_install hook
 */
function hosting_site_post_hosting_install_task($task, $data) {
  if ($task->ref->type == 'site') {
    $context = $data['context'];
    $packages = $context['packages'];
    hosting_package_sync($packages);
    hosting_package_instance_sync($task->ref->nid, $task->ref->type, $packages);
    $task->ref->site_status = HOSTING_SITE_ENABLED;
    $task->ref->no_verify = TRUE;
    $task->ref->verified = REQUEST_TIME;
    $task->ref->db_name = $data['self']['db_name'];
    if ($context['cron_key']) {
      $task->ref->cron_key = $context['cron_key'];
    }
    node_save($task->ref);

    if ($context['login_link']) {
      drush_log($context['login_link']);
      // we need to store the timestamp too because expire only means it can be deleted
      // after an amount of time, not will be deleted.
      $cache = array(
        'expire' => strtotime("+24 hours"),
        'link' => $context['login_link'],
      );
      cache_set('hosting:site:' . $task->ref->nid . ':login_link', $cache, 'cache', $cache['expire'] );
    }
  }
}

/**
 * Implements the hosting_failed_install hook
 */
function hosting_site_hosting_install_task_rollback($task, $data) {
  // @TODO : we need to check the returned list of errors, not the code.
  if (drush_cmp_error('PROVISION_DRUPAL_SITE_INSTALLED')) {
    // Site has already been installed. Try to import instead.
    drush_log(dt("This site appears to be installed already. Generating an import task."));
    hosting_add_task($task->rid, 'import');
  }
  else {
    $task->ref->no_verify = TRUE;
    $task->ref->site_status = HOSTING_SITE_DISABLED;
    node_save($task->ref);
  }
}

/**
 * Implements hook_hosting_post_disable().
 */
function hosting_site_post_hosting_disable_task($task, $data) {
  if (!empty($data['context']['backup_file']) && $data->ref->type == 'site') {
    $platform = node_load($task->ref->platform);
    hosting_site_add_backup($task->ref->nid, $platform->web_server, $data['context']['backup_file'], t('Generated before being disabled'), $data['context']['backup_file_size']);
  }
  $task->ref->site_status = HOSTING_SITE_DISABLED;
  $task->ref->no_verify = TRUE;
  node_save($task->ref);

  // Record the backup created during disable.
  $task->task_args['description'] = t('Pre-disable backup');
  hosting_site_post_hosting_backup_task($task, $data);
}

/**
 * Implements hook_hosting_post_restore().
 */
function hosting_site_post_hosting_restore_task($task, $data) {
  if (!empty($data['context']['backup_file']) && $task->ref->type == 'site') {
    $platform = node_load($task->ref->platform);
    // The pre-restore safety copy is forced to the restorable mode by the
    // backend; record the mode it publishes so the row stops rendering as an
    // unlabelled archive of unknown completeness.
    hosting_site_add_backup($task->ref->nid, $platform->web_server, $data['context']['backup_file'], t('Generated before being restored to a previous version'), $data['context']['backup_file_size'], hosting_site_backup_mode_from_context($data));
  }
}

/**
 * Implements hook_hosting_post_enable().
 */
function hosting_site_post_hosting_enable_task($task, $data) {
  $task->ref->site_status = HOSTING_SITE_ENABLED;
  $task->ref->no_verify = TRUE;
  node_save($task->ref);
}

/**
 * Implements hook_hosting_post_DELETE().
 */
function hosting_site_post_hosting_delete_task($task, $data) {
  if ($task->ref->type == 'site') {
    $task->ref->site_status = HOSTING_SITE_DELETED;
    $task->ref->no_verify = TRUE;
    hosting_context_delete($task->ref->nid);
    node_save($task->ref);

    // Record the backup created during delete.
    $task->task_args['description'] = t('Pre-delete backup');
    hosting_site_post_hosting_backup_task($task, $data);

    // Clean up package instances
    hosting_package_instance_sync($task->ref->nid, 'site');
  }
}

/**
 * Implements hook_post_hosting_TASK_TYPE_task().
 *
 * Retires the pseudo-site after a successful leftover purge (D-035),
 * mirroring the standard site delete post-task, and drops the leftover from
 * the platform's recorded list immediately (the next verify would too, but
 * the button should go now).
 */
function hosting_site_post_hosting_leftover_purge_task($task, $data) {
  if (!isset($task->ref) || $task->ref->type != 'site') {
    return;
  }
  $nid = $task->ref->nid;
  if (!hosting_site_is_leftover($nid)) {
    return;
  }
  $info = hosting_site_leftover_info($nid);

  $task->ref->site_status = HOSTING_SITE_DELETED;
  $task->ref->no_verify = TRUE;
  hosting_context_delete($nid);
  node_save($task->ref);
  variable_del('hosting_leftover_site_' . $nid);

  if (!empty($info['platform']) && !empty($info['leftover'])) {
    $leftover_var = 'hosting_leftover_platform_' . $info['platform'];
    $list = variable_get($leftover_var, array());
    if (is_array($list) && isset($list[$info['leftover']])) {
      unset($list[$info['leftover']]);
      if (count($list)) {
        variable_set($leftover_var, $list);
      }
      else {
        variable_del($leftover_var);
      }
    }
  }
}

/**
 * Implements hook_hosting_post_backup().
 *
 * Adds the data file that was saved to the site backup history.
 * This is needed to be able to restore.
 */
function hosting_site_post_hosting_backup_task($task, $data) {
  if (!empty($data['context']['backup_file']) && $task->ref->type == 'site') {
    $platform = node_load($task->ref->platform);

    $desc = $task->task_args['description'];
    $desc = ($desc) ? $desc : t('Generated on request');
    $backup_mode = hosting_site_backup_mode_from_context($data);
    hosting_site_add_backup($task->ref->nid, $platform->web_server, $data['context']['backup_file'], $desc, $data['context']['backup_file_size'], $backup_mode);
  }
}


/**
 * Implements hook_post_hosting_import_task().
 */
function hosting_site_post_hosting_import_task($task, $data) {
  $site_nid = hosting_drush_import($task->ref->hosting_name);

  // Update the site node with the database name.
  $node = node_load($site_nid);
  $node->db_name = $data['self']['db_name'];
  node_save($node);

  $packages = $data['context']['packages'];
  hosting_package_sync($packages);
  hosting_package_instance_sync($task->ref->nid, $task->ref->type, $packages);
}

/**
 * Implements hook_post_hosting_verify_task().
 */
function hosting_site_post_hosting_verify_task($task, $data) {
  if ($task->ref->type == 'site') {
    $task->ref->verified = REQUEST_TIME;
    $task->ref->no_verify = TRUE;
    $task->ref->db_name = $data['self']['db_name'];
    if ($data['context']['cron_key']) {
      $task->ref->cron_key = $data['context']['cron_key'];
    }
    node_save($task->ref);

    $context = $data['context'];
    $packages = $context['packages'];
    hosting_package_sync($packages);
    hosting_package_instance_sync($task->ref->nid, $task->ref->type, $packages);
  }
}

/**
 * Implements the hosting_failed_install hook
 */
function hosting_site_hosting_verify_task_rollback($task, $data) {
  if ($task->ref->type == 'site') {
    $task->ref->no_verify = TRUE;
    $task->ref->verified = 0;
    node_save($task->ref);
  }
}

/**
 * Implements hook_hosting_import_task_rollback().
 */
function hosting_site_hosting_import_task_rollback($task, $data) {
  if ($task->ref->type == 'site') {
    $task->ref->no_verify = TRUE;
    $task->ref->verified = 0;
    $task->ref->site_status = HOSTING_SITE_DISABLED;
    node_save($task->ref);
  }
}

/**
 * Implements hook_post_hosting_login_reset_task().
 */
function hosting_site_post_hosting_login_reset_task($task, $data) {
  if ($task->ref->type == 'site') {
    $task->ref->no_verify = TRUE;
    node_save($task->ref);

    $context = $data['context'];
    if ($context['login_link']) {
      // we need to store the timestamp too because expire only means it can be deleted
      // after an amount of time, not will be deleted.
      $cache = array(
        'expire' => strtotime("+24 hours"),
        'link' => $context['login_link'],
      );
      cache_set('hosting:site:' . $task->ref->nid . ':login_link', $cache, 'cache', $cache['expire'] );
    }
  }
}

/**
 * Implements the hosting_post_backup_delete hook
 */
function hosting_site_post_hosting_backup_delete_task($task, $data) {
  if ($task->ref->type == 'site') {
    foreach ($task->task_args as $bid => $filename) {
      if ($filename !== '0') {
        hosting_site_delete_backup($bid);
      }
    }
  }
}
