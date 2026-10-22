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

  $node = _hosting_resume_own_site();

  // Fix the old platform first.
  $old_platform_id = $platform_id = db_query('SELECT nid FROM {hosting_platform} WHERE publish_path = :publish_path AND status = :status', array(
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
    $platform = _hosting_resume_platform_template($node, $old_platform_id);
    if ($platform) {
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
  $named = TRUE;
  $why = '';
  $why_site = '';
  if ($platform_id) {
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

    if ($node) {
      drush_log('fixing platform for site node');
      $node->verified = 0;
      $node->platform = $platform_id;
      node_save($node);
    }
    else {
      $node = _hosting_resume_create_own_site($platform_id, $why_site);
    }
  }
  else {
    $why_site = dt('no platform record could be made for @path', array('@path' => $platform_root));
  }
  provision_backend_invoke(d()->name, 'hosting-setup');

  if (!$node) {
    drush_set_error('HOSTING_RESUME_SITE_RECORD', dt('The panel @site has no site record: no live site node holds the context name @name or the domain @uri, and none could be made (@why). The panel runs from @path but its record does not follow it; the queue and command line keep working until a Verify of the panel is needed.', array(
      '@site' => d()->name,
      '@name' => ltrim(d()->name, '@'),
      '@uri' => d()->uri,
      '@why' => $why_site,
      '@path' => $platform_root,
    )));
  }
  if (!$named) {
    drush_set_error('HOSTING_RESUME_PLATFORM_NAME', dt('The new platform @path (node @nid) could not take the context name @name: @why. The site @site runs on node @nid under the name the panel gave it; release or rename the holder, then verify the platform.', array(
      '@path' => $platform_root,
      '@nid' => $platform_id,
      '@name' => ltrim($new_platform, '@'),
      '@why' => $why,
      '@site' => d()->name,
    )));
  }
  if (!$node || !$named) {
    return FALSE;
  }
}

/**
 * Find the panel's own site record.
 *
 * It is the node registered under the panel's context name. A panel whose
 * database lost that row (a move between servers, a repair by hand) may
 * still hold its live site node under its own domain: that node takes the
 * name back. A record under any other domain is never taken: its next
 * Verify would write that domain into the panel's alias.
 *
 * @return object|bool
 *   The site node, or FALSE when the panel has no live record of itself.
 */
function _hosting_resume_own_site() {
  $name = ltrim(d()->name, '@');
  $node = hosting_context_load($name);
  if ($node && $node->type == 'site' && isset($node->site_status) && $node->site_status != HOSTING_SITE_DELETED) {
    return $node;
  }
  $uri = hosting_site_get_domain(d()->uri);
  $nids = db_query("SELECT n.nid FROM {node} n INNER JOIN {hosting_site} s ON s.vid = n.vid WHERE n.type = 'site' AND n.title = :title AND s.status <> :deleted", array(
    ':title' => $uri,
    ':deleted' => HOSTING_SITE_DELETED,
  ))->fetchCol();
  if (count($nids) > 1) {
    drush_log(dt('Live site nodes @nids all carry the domain @uri; none is taken as the panel record.', array('@nids' => implode(', ', $nids), '@uri' => $uri)), 'warning');
    return FALSE;
  }
  if (!$nids) {
    drush_log(dt('No live site node holds the context name @name or the domain @uri.', array('@name' => $name, '@uri' => $uri)), 'warning');
    return FALSE;
  }
  $nid = reset($nids);
  if (!hosting_context_register($nid, $name, TRUE)) {
    return FALSE;
  }
  variable_set('aegir_hostmaster_site_nid', $nid);
  drush_log(dt('The site node @nid of @uri had lost the context name @name; it holds it again.', array('@nid' => $nid, '@uri' => $uri, '@name' => $name)), 'warning');
  return node_load($nid, NULL, TRUE);
}

/**
 * The node a new panel platform record copies its settings from.
 *
 * The site's own platform first, as always; without one, the old platform
 * found by its path; without that, a new record on the web server the
 * panel's platform context names, as the install makes it.
 *
 * @param object|bool $site
 *   The panel's site node, or FALSE.
 * @param int|bool $old_platform_id
 *   The enabled platform node at the old root, or FALSE.
 *
 * @return object|bool
 *   A platform node to save as new, or FALSE when the web server has no
 *   record either.
 */
function _hosting_resume_platform_template($site, $old_platform_id) {
  foreach (array($site ? $site->platform : 0, $old_platform_id) as $nid) {
    if ($nid && ($platform = node_load($nid)) && $platform->type == 'platform') {
      return $platform;
    }
  }
  $server = hosting_context_load(d()->platform->web_server->name);
  if (!$server || $server->type != 'server') {
    drush_log(dt('The web server @name has no record: no platform record can be made for the panel.', array('@name' => d()->platform->web_server->name)), 'error');
    return FALSE;
  }
  drush_log(dt('No platform record to copy: a new one is made on @server.', array('@server' => $server->title)), 'warning');
  $platform = new stdClass();
  $platform->uid = 1;
  $platform->status = 1;
  $platform->web_server = $server->nid;
  $platform->platform_status = HOSTING_PLATFORM_ENABLED;
  $platform->makefile = '';
  $platform->make_working_copy = 0;
  return $platform;
}

/**
 * Make the panel's own site record again, the way its install made it.
 *
 * Every value is the one the panel's alias holds now, so the Verify queued
 * here (as for a record that moved) writes the alias back as it is.
 *
 * @param int $platform_id
 *   The panel's new platform node.
 * @param string $why
 *   Set to the reason when no record is made.
 *
 * @return object|bool
 *   The new site node, or FALSE.
 */
function _hosting_resume_create_own_site($platform_id, &$why) {
  $db_server = hosting_context_load(d()->db_server->name);
  if (!$db_server || $db_server->type != 'server') {
    $why = dt('the database server @name has no record', array('@name' => d()->db_server->name));
    return FALSE;
  }
  // The client the alias names; else the one the install gave the panel.
  $client = d()->client_name ? hosting_get_client_by_uname(d()->client_name) : FALSE;
  if (!$client) {
    $client = node_load(variable_get('hosting_admin_client', 1));
  }
  if (!$client || $client->type != 'client') {
    $client = node_load(hosting_get_default_client());
  }
  if (!$client || $client->type != 'client') {
    $why = dt('the client @name has no record', array('@name' => d()->client_name));
    return FALSE;
  }
  $profile = db_query("SELECT p.nid FROM {hosting_package} p INNER JOIN {node} n ON n.vid = p.vid WHERE p.short_name = :name AND p.package_type = 'profile'", array(':name' => d()->profile))->fetchField();

  $node = new stdClass();
  $node->uid = 1;
  $node->type = 'site';
  $node->title = hosting_site_get_domain(d()->uri);
  $node->hosting_name = ltrim(d()->name, '@');
  $node->platform = $platform_id;
  $node->client = $client->nid;
  $node->db_server = $db_server->nid;
  $node->db_name = '';
  $node->profile = $profile ? $profile : 0;
  $node->site_language = d()->language ? d()->language : 'en';
  $node->site_status = HOSTING_SITE_ENABLED;
  $node->verified = 0;
  $node->status = 1;
  // The panel is installed and serving: no install or import task.
  $node->no_auto_task = TRUE;
  try {
    node_save($node);
  }
  catch (Exception $e) {
    $why = $e->getMessage();
    return FALSE;
  }
  variable_set('aegir_hostmaster_site_nid', $node->nid);
  hosting_add_task($node->nid, 'verify');
  drush_log(dt('The panel had no site record: node @nid made for @uri on platform node @platform.', array('@nid' => $node->nid, '@uri' => $node->title, '@platform' => $platform_id)), 'warning');
  return $node;
}
