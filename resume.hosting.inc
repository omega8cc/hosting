<?php

/**
 * @file
 * Drush include for the Hosting module's hosting resume command.
 */

include_once 'hosting.inc';

/**
 * Drush command to resume the Aegir frontend site.
 */
function drush_hosting_resume() {
  drush_log('bootstrapping drupal');
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
  $server_name = drush_get_option('server_name');
  $old_platform = drush_get_option('old_platform_name');
  $new_platform = drush_get_option('new_platform_name');

  $node = hosting_context_load(d()->name);

  // Fix the old platform first.
  $platform_id = db_query('SELECT nid FROM {hosting_platform} WHERE publish_path = :publish_path AND status = :status', array(
    ':publish_path' => d($old_platform)->root,
    ':status' => HOSTING_PLATFORM_ENABLED,
  ))->fetchField();
  if ($platform_id) {
    if (!hosting_context_register($platform_id, ltrim($old_platform, '@'), TRUE)) {
      drush_log(dt('The old platform node @nid keeps its current context name.', array('@nid' => $platform_id)), 'warning');
    }
  }
  else {
    drush_log(dt('Old platform not found in path %path, not setting context %context', array('%path' => d($old_platform)->root, '%context' => ltrim($old_platform, '@'))), 'warning');
  }

  // If the new platform doesn't exist, create it.
  $platform_root = d()->root;
  if (!($platform_id = db_query('SELECT nid FROM {hosting_platform} WHERE publish_path = :publish_path', array(':publish_path' => $platform_root))->fetchField())) {
    drush_log(dt('Platform not found for path %path, adding platform node', array('%path' => $platform_root)));
    // Inherit settings from current platform node.
    $platform = node_load($node->platform);
    $platform->type = 'platform';
    unset($platform->nid);
    unset($platform->vid);
    $platform->publish_path = $platform_root;
    $platform->verified = 0;
    $platform->title = basename(rtrim($platform_root, '/'));
    if (preg_match("/^([0-9]+)$/i", $platform->title)) {
      $platform->title = 'Aegir Hostmaster ' . $platform->title;
    }
    elseif (preg_match("/hostmaster/i", $platform->title)) {
      $platform->title = 'Aegir Hostmaster';
    }
    // The files have moved already: the record must follow even when the
    // name built from the title is held by a live node (reported below).
    $platform->context_report_only = TRUE;
    node_save($platform);
    $platform_id = $platform->nid;
  }
  else {
    drush_log(dt('Platform found for path %path: #%id', array('%path' => $platform_root, '%id' => $platform_id)));
    $platform = node_load($platform_id);
  }

  // The site record follows the files whatever happens to the name: a
  // panel whose record still names the old platform while its files live in
  // the new one loses its command line and its queue, because the next
  // Verify of the site writes its alias for the old root, where no
  // drushrc.php is left.
  $why = '';
  try {
    $named = hosting_context_register($platform_id, ltrim($new_platform, '@'), TRUE);
    if (!$named) {
      $why = dt('another live node holds it');
    }
  }
  catch (PDOException $e) {
    $named = FALSE;
    $why = $e->getMessage();
  }

  drush_log('fixing platform for site node');
  $node->verified = 0;
  $node->platform = $platform_id;
  node_save($node);
  provision_backend_invoke(d()->name, 'hosting-setup');

  if (!$named) {
    return drush_set_error('HOSTING_RESUME_PLATFORM_NAME', dt('The new platform @path (node @nid) could not take the context name @name: @why. The site @site runs on node @nid under the name the panel gave it; release or rename the holder, then verify the platform.', array(
      '@path' => $platform_root,
      '@nid' => $platform_id,
      '@name' => ltrim($new_platform, '@'),
      '@why' => $why,
      '@site' => d()->name,
    )));
  }
}
