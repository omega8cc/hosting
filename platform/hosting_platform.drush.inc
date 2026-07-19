<?php
/**
 * @file
 * Implement drush hooks for the Platforms module.
 */

/**
 * Implements hook_hosting_TASK_OBJECT_context_options().
 */
function hosting_hosting_platform_context_options(&$task) {
  $task->context_options['server'] = '@server_master';
  $task->context_options['web_server'] = hosting_context_name($task->ref->web_server);
  // Trim the path so we dont pass extra spaces.
  $task->context_options['root'] = trim($task->ref->publish_path, " ");
  if ($task->ref->makefile) {
    $task->context_options['makefile'] = $task->ref->makefile;
  }
  if (isset($task->ref->make_working_copy) && !empty($task->ref->make_working_copy)) {
    $task->context_options['make_working_copy'] = $task->ref->make_working_copy;
  }
  else {
    $task->context_options['make_working_copy'] = 'null';
  }
}

/**
 * Implements hook_drush_context_import().
 */
function hosting_platform_drush_context_import($context, &$node) {
  if ($context->type == 'platform') {
    if (!isset($node->title)) {
      $node->title = str_replace('platform_', '', trim($context->name, '@'));
    }
    $node->web_server = hosting_context_nid($context->web_server);
    $node->publish_path = $context->root;
    $node->makefile = $context->makefile;
    if (isset($context->make_working_copy) && !empty($context->make_working_copy)) {
      $node->make_working_copy = $context->make_working_copy;
    }
  }
}

/**
 * Implements hook_post_hosting_TASK_TYPE_task().
 *
 * Sets the platform verified timestamp, to discren when it was verified.
 * Imports all the profiles and modules into package and package release nodes.
 */
function hosting_platform_post_hosting_verify_task($task, $data) {
  $node = $task->ref;
  if ($node->type == 'platform') {
    $context = !empty($data['context']) ? $data['context'] : array();
    $packages = !empty($context['packages']) ? $context['packages'] : array();

    // Lock platforms by default.
    if ($node->verified == 0 && variable_get('hosting_lock_platforms_by_default', FALSE)) {
      $node->platform_status = HOSTING_PLATFORM_LOCKED;
    }

    // Set verified flag on platform, to let it know it has been checked.
    $node->verified = REQUEST_TIME;

    /**
     * If we are verifying a Locked platform (i.e if the publish_path has changed),
     * don't reset the status to Enabled. We don't need to check whether a platform
     * is deleted here for the same reason, because we don't allow a deleted platform
     * to be reverified.
     */
    if ($node->platform_status != HOSTING_PLATFORM_LOCKED) {
      $node->platform_status = HOSTING_PLATFORM_ENABLED;
    }
    $node->no_verify = TRUE;
    // Save the platform being verified.
    node_save($node);

    hosting_package_sync($packages['base']);
    hosting_package_sync($packages['sites-all']);
    hosting_package_instance_sync($node->nid, $task->ref->type, $packages['base'], $packages['sites-all']);
    if (!empty($context['profiles']) && is_array($context['profiles'])) {
      foreach ($context['profiles'] as $profile) {
        hosting_package_sync($packages['profiles'][$profile]);
        $instance = hosting_package_instance_load(
          array('p.short_name' => $profile, 'i.rid' => $node->nid, 'p.package_type' => 'profile'));
        hosting_package_instance_sync($instance->iid, $instance->package_type, $packages['base'], $packages['profiles'][$profile], $packages['sites-all']);
        // @todo : package instances of modules to profiles. <-- huh?
      }
    }

    // If configured to do so, import all sites found on this platform.
    if (variable_get('hosting_platform_automatic_site_import', TRUE) && !empty($context['sites']) && is_array($context['sites'])) {
      foreach ($context['sites'] as $url) {
        // Never adopt revert-artifact leftovers as real sites: a deploy/
        // restore/migrate that aborts mid-swap strands sites/<name>.restore
        // (the swap-staging path) with a settings.php inside, and the backend
        // find-sites scan deliberately keeps reporting it (the task revert
        // procedure depends on the directory staying discoverable). Importing
        // one creates a phantom site node pointing at a non-site. The
        // hosting_backdrop module can expose such a leftover as a purgeable
        // pseudo-site explicitly.
        if (hosting_leftover_name_is_artifact($url)) {
          drush_log(dt("Skipped importing !domain: revert-artifact leftover from an interrupted task, not a live site.", array('!domain' => $url)), 'info');
          continue;
        }
        if (hosting_domain_allowed($url)) {
          // Import any sites that have not been created yet.
          $site = new stdClass();
          $site->type = 'site';
          $site->site_status = HOSTING_SITE_QUEUED;
          $site->import = TRUE;
          $site->title = $url;
          $site->platform = $node->nid;
          $site->client = HOSTING_DEFAULT_CLIENT;
          $site->db_server = HOSTING_DEFAULT_DB_SERVER;
          node_save($site);
          drush_log(dt("Imported existing site !domain", array('!domain' => _hosting_node_link($site->nid))));
          hosting_package_instance_sync($site->nid, $site->type, $packages['base']);
        }
      }
    }

    // Refresh the platform's revert-artifact leftover list (D-035) from the
    // same backend sites scan the auto-import loop consumes. Recording them
    // is what makes the "Import leftover" platform task able to offer them;
    // the list self-heals (a leftover removed out-of-band disappears on the
    // next verify). Only backend-manageable names are recorded — an artifact
    // the strict backend gate would refuse is logged instead of stranding an
    // un-purgeable pseudo-site later.
    $leftover_var = 'hosting_leftover_platform_' . $node->nid;
    $previous = variable_get($leftover_var, array());
    $previous = is_array($previous) ? $previous : array();
    $leftovers = array();
    if (!empty($context['sites']) && is_array($context['sites'])) {
      foreach ($context['sites'] as $url) {
        if (!hosting_leftover_name_is_artifact($url)) {
          continue;
        }
        if (!hosting_leftover_name_is_manageable($url)) {
          drush_log(dt("Leftover !name is not a manageable name; remove sites/!name on the platform manually.", array('!name' => $url)), 'warning');
          continue;
        }
        // Keep the first-seen stamp across verifies.
        $leftovers[$url] = isset($previous[$url]['seen'])
          ? array('seen' => $previous[$url]['seen']) : array('seen' => REQUEST_TIME);
      }
    }
    if (count($leftovers)) {
      if ($leftovers !== $previous) {
        variable_set($leftover_var, $leftovers);
      }
      drush_log(dt('Recorded !count revert-artifact leftover(s) on this platform: !names. Use the "Import leftover" platform task to expose one for purging.',
        array('!count' => count($leftovers), '!names' => implode(', ', array_keys($leftovers)))), 'info');
    }
    elseif (count($previous)) {
      variable_del($leftover_var);
    }
  }

  // When a server is verified, queue up a verify task for every platform.
  if ($task->ref->type == 'server') {
    $query = db_query("SELECT nid FROM {hosting_platform} WHERE status = :status AND web_server = :web_server", array(':status' => HOSTING_PLATFORM_ENABLED, ':web_server' => $task->ref->nid));
    while ($nid = $query->fetch()) {
      hosting_add_task($nid->nid, 'verify');
    }
  }
}

/**
 * Implements hook_hosting_post_DELETE().
 */
function hosting_platform_post_hosting_delete_task($task, $data) {
  $task->ref->platform_status = HOSTING_PLATFORM_DELETED;
  $task->ref->no_verify = TRUE;
  node_save($task->ref);
  hosting_context_delete($task->ref->nid);
  db_delete('hosting_platform_client_access')
    ->condition('pid', $task->ref->nid)
    ->execute();
  // Clean up package instances.
  hosting_package_instance_sync($task->ref->nid, 'platform');
  // Drop the recorded revert-artifact leftover list (D-035).
  if (variable_get('hosting_leftover_platform_' . $task->ref->nid, NULL) !== NULL) {
    variable_del('hosting_leftover_platform_' . $task->ref->nid);
  }
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_platform_post_hosting_lock_task($task, $data) {
  $task->ref->platform_status = HOSTING_PLATFORM_LOCKED;
  $task->ref->no_verify = TRUE;
  node_save($task->ref);
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_platform_post_hosting_unlock_task($task, $data) {
  $task->ref->platform_status = HOSTING_PLATFORM_ENABLED;
  $task->ref->no_verify = TRUE;
  node_save($task->ref);
}

/**
 * Implements drush_HOOK_pre_hosting_task().
 *
 * Populates the backend invocation for the leftover_import task (D-035): the
 * task rides its platform node's own context — only the leftover name needs
 * forwarding. Fail-closed re-checks: the name must still be in the platform's
 * recorded list (a purge or manual cleanup since form time makes the task
 * fail here, not backend-side with a confusing path error).
 */
function drush_hosting_platform_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if ($task->task_type != 'leftover_import') {
    return;
  }
  if ($task->ref->type != 'platform') {
    drush_set_error('HOSTING_LEFTOVER_REF',
      dt('The leftover_import task runs on platform nodes only.'));
    return;
  }
  $name = isset($task->task_args['leftover']) ? $task->task_args['leftover'] : '';
  if (!hosting_leftover_name_is_manageable($name)) {
    drush_set_error('HOSTING_LEFTOVER_NAME',
      dt('No valid leftover name in the task arguments.'));
    return;
  }
  $recorded = hosting_platform_leftover_list($task->ref->nid);
  if (!isset($recorded[$name])) {
    drush_set_error('HOSTING_LEFTOVER_GONE',
      dt('@name is no longer recorded on this platform (purged or cleaned up since?). Re-verify the platform to refresh the list.',
      array('@name' => $name)));
    return;
  }
  $task->args = array($name);
}

/**
 * Implements hook_post_hosting_TASK_TYPE_task().
 *
 * Registers the pseudo-site node for an exposed leftover (D-035). The node is
 * a regular hosting_site record so it appears in the UI (badged and marked),
 * but it is created DISABLED with no_auto_task (context registered, no
 * install/import queued — there is nothing to install) and with SSL/cron
 * forced dark. Client, db server and ownership are inherited from the live
 * base site when one exists (the leftover belongs to its lineage), so the
 * owning client sees the record.
 */
function hosting_platform_post_hosting_leftover_import_task($task, $data) {
  if (!isset($task->ref) || $task->ref->type != 'platform') {
    return;
  }
  $name = isset($task->task_args['leftover']) ? $task->task_args['leftover'] : '';
  if (!hosting_leftover_name_is_manageable($name)) {
    drush_log(dt('[leftover-import] No valid leftover name in the task arguments; nothing registered.'), 'warning');
    return;
  }
  if (hosting_context_load($name)) {
    drush_log(dt('[leftover-import] @name is already registered; nothing to do.', array('@name' => $name)), 'ok');
    return;
  }

  $info = array();
  if (isset($data['context']['leftover_info']) && is_array($data['context']['leftover_info'])) {
    $info = $data['context']['leftover_info'];
  }

  // Inherit ownership from the live base site when it exists.
  $base = preg_replace('/\.(restore|original)$/i', '', $name);
  $base_site = hosting_get_site_by_url($base);

  $site = new stdClass();
  $site->type = 'site';
  $site->status = 1;
  $site->uid = is_object($base_site) && isset($base_site->uid) ? $base_site->uid : 1;
  $site->title = $name;
  $site->hosting_name = $name;
  $site->platform = $task->ref->nid;
  $site->site_status = HOSTING_SITE_DISABLED;
  $site->no_auto_task = TRUE;
  $site->verified = 0;
  $site->profile = 0;
  $site->client = is_object($base_site) && !empty($base_site->client) ? $base_site->client : HOSTING_DEFAULT_CLIENT;
  $site->db_server = is_object($base_site) && !empty($base_site->db_server) ? $base_site->db_server : HOSTING_DEFAULT_DB_SERVER;
  $site->db_name = isset($info['db_name']) && is_string($info['db_name']) ? $info['db_name'] : '';
  $site->aliases = array();
  $site->redirection = FALSE;
  if (defined('HOSTING_SSL_DISABLED')) {
    $site->ssl_enabled = HOSTING_SSL_DISABLED;
    $site->ssl_key = 0;
  }
  if (defined('ADV_CRON_TURNED_OFF')) {
    $site->cron_interval = ADV_CRON_TURNED_OFF;
  }
  node_save($site);

  variable_set('hosting_leftover_site_' . $site->nid, array(
    'platform' => $task->ref->nid,
    'leftover' => $name,
    'db_name' => $site->db_name,
    'db_host' => isset($info['db_host']) && is_string($info['db_host']) ? $info['db_host'] : '',
    'exposed' => REQUEST_TIME,
  ));

  drush_log(dt('[leftover-import] Registered @name as pseudo-site !link — run its Purge leftover task to remove the artifact from the server.',
    array('@name' => $name, '!link' => _hosting_node_link($site->nid))), 'ok');
}
