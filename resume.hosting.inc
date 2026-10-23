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

  // The panel's own site record. Without one, what a new one needs is read
  // before any platform record is made: a platform Verify imports every site
  // it finds with no live record, so a new panel platform verified while the
  // panel has none would bring the panel in as an ordinary site.
  $why_site = '';
  $why_make = '';
  $node = _hosting_resume_own_site($why_site);
  $values = (!$node && !$why_site) ? _hosting_resume_own_site_values($why_make) : FALSE;

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
  $platform = FALSE;
  if (!($platform_id = db_query('SELECT nid FROM {hosting_platform} WHERE publish_path = :publish_path', array(':publish_path' => $platform_root))->fetchField())) {
    drush_log(dt('Platform not found for path %path, adding platform node', array('%path' => $platform_root)));
    if ($node || $values) {
      $platform = _hosting_resume_platform_template($node, $old_platform_id);
    }
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
      // The panel runs from it, whatever status a parked template carries.
      if (isset($platform->platform_status) && $platform->platform_status == HOSTING_PLATFORM_DELETED) {
        $platform->platform_status = HOSTING_PLATFORM_ENABLED;
      }
      // Its Verify waits for the panel's record (above).
      if (!$node) {
        $platform->no_verify = TRUE;
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
    elseif ($values) {
      $node = _hosting_resume_create_own_site($values, $platform_id, $why_make);
      if ($node && !empty($platform->no_verify)) {
        hosting_add_task($platform_id, 'verify');
      }
    }
  }
  elseif ($values) {
    $why_make = dt('there is no platform record to put it on');
  }
  provision_backend_invoke(d()->name, 'hosting-setup');

  if (!$platform_id) {
    drush_set_error('HOSTING_RESUME_PLATFORM', dt('No platform record was made for @path, where the panel @site runs now; @record.', array(
      '@path' => $platform_root,
      '@site' => d()->name,
      '@record' => $node ? dt('its site record (node @nid) still names the old platform', array('@nid' => $node->nid)) : dt('it has no site record either'),
    )));
  }
  if (!$node) {
    $args = array(
      '@site' => d()->name,
      '@name' => ltrim(d()->name, '@'),
      '@uri' => d()->uri,
      '@why' => $why_site ? $why_site : $why_make,
      '@path' => $platform_root,
    );
    if ($why_site) {
      $message = dt('The panel @site has no site record it may use: @why. None is made while that stands.', $args);
    }
    else {
      $message = dt('The panel @site has no site record: no live site node holds the context name @name or the domain @uri, and none was made: @why.', $args);
    }
    drush_set_error('HOSTING_RESUME_SITE_RECORD', $message . ' ' . dt('The panel runs from @path but its record does not follow it; the queue and command line keep working until a Verify of the panel is needed.', $args));
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
  if (!$platform_id || !$node || !$named) {
    return FALSE;
  }
}

/**
 * Find the panel's own site record.
 *
 * It is the node registered under the panel's context name. A panel whose
 * database lost that row (a move between servers, a repair by hand) may
 * still hold its live site node under its own domain, profile and database
 * server: that node takes the name back. Any other record is never taken:
 * its next Verify would write its own values into the panel's alias.
 *
 * @param string $why
 *   Set when a record exists that cannot be taken, so none may be made.
 *
 * @return object|bool
 *   The site node, or FALSE.
 */
function _hosting_resume_own_site(&$why) {
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
    $why = dt('live site nodes @nids all carry the domain @uri', array('@nids' => implode(', ', $nids), '@uri' => $uri));
    return FALSE;
  }
  if (!$nids) {
    drush_log(dt('No live site node holds the context name @name or the domain @uri.', array('@name' => $name, '@uri' => $uri)), 'warning');
    return FALSE;
  }
  // A panel without its record does not hold its domain: anyone who may add
  // sites could have taken it. Only a record on the panel's profile and
  // database server is the panel's own.
  $nid = reset($nids);
  $site = node_load($nid);
  $profile = empty($site->profile) ? FALSE : node_load($site->profile);
  $db_server = hosting_context_load(d()->db_server->name);
  $short_name = ($profile && isset($profile->short_name)) ? $profile->short_name : '';
  if ($short_name !== d()->profile || !$db_server || $site->db_server != $db_server->nid) {
    $why = dt('site node @nid carries the domain @uri with the profile "@has" on database server node @hasdb, where the panel runs the profile "@profile" on @server', array(
      '@nid' => $nid,
      '@uri' => $uri,
      '@has' => $short_name,
      '@hasdb' => $site->db_server,
      '@profile' => d()->profile,
      '@server' => d()->db_server->name,
    ));
    return FALSE;
  }
  if (!hosting_context_register($nid, $name, TRUE)) {
    $why = dt('site node @nid carries the domain @uri but cannot take the context name @name', array('@nid' => $nid, '@uri' => $uri, '@name' => $name));
    return FALSE;
  }
  variable_set('aegir_hostmaster_site_nid', $nid);
  drush_log(dt('The site node @nid of @uri had lost the context name @name; it holds it again.', array('@nid' => $nid, '@uri' => $uri, '@name' => $name)), 'warning');
  return node_load($nid, NULL, TRUE);
}

/**
 * What a new record of the panel needs, read from records that exist.
 *
 * @param string $why
 *   Set to the reason when something is missing.
 *
 * @return array|bool
 *   The database server and client nids, or FALSE.
 */
function _hosting_resume_own_site_values(&$why) {
  $name = ltrim(d()->name, '@');
  $holder = (int) db_query("SELECT nid FROM {hosting_context} WHERE name = :name", array(':name' => $name))->fetchField();
  if ($holder && !hosting_context_holder_is_stale($holder)) {
    $why = dt('the context name @name is held by node @holder, which is not the panel', array('@name' => $name, '@holder' => $holder));
    return FALSE;
  }
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
  return array('db_server' => $db_server->nid, 'client' => $client->nid);
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
 * @param array $values
 *   The database server and client nids (_hosting_resume_own_site_values()).
 * @param int $platform_id
 *   The panel's new platform node.
 * @param string $why
 *   Set to the reason when no record is made.
 *
 * @return object|bool
 *   The new site node, or FALSE.
 */
function _hosting_resume_create_own_site($values, $platform_id, &$why) {
  // A panel with no site record loses its profile's package to the
  // collector once no instance names it: it is made again as the install
  // makes it, and the record that names it keeps it from then on. The next
  // platform Verify finds it by name and fills in its details.
  $profile = d()->profile ? hosting_get_package(d()->profile, 'profile') : FALSE;
  if (!$profile && d()->profile) {
    $profile = new stdClass();
    $profile->uid = 1;
    $profile->type = 'package';
    $profile->title = d()->profile;
    $profile->package_type = 'profile';
    $profile->short_name = d()->profile;
    $profile->old_short_name = d()->profile;
    $profile->description = '';
    $profile->status = 1;
    try {
      node_save($profile);
    }
    catch (Exception $e) {
      $why = $e->getMessage();
      return FALSE;
    }
  }

  $node = new stdClass();
  $node->uid = 1;
  $node->type = 'site';
  $node->title = hosting_site_get_domain(d()->uri);
  $node->hosting_name = ltrim(d()->name, '@');
  $node->platform = $platform_id;
  $node->client = $values['client'];
  $node->db_server = $values['db_server'];
  $node->db_name = '';
  $node->profile = $profile ? $profile->nid : 0;
  $node->site_language = d()->language ? d()->language : 'en';
  // What other modules keep in the alias (aliases, basic auth and the like)
  // comes in through their import hooks, as hosting-import takes it. The
  // site hook is left out: it would look up or make the records set above.
  foreach (drush_command_implements('drush_context_import') as $module) {
    if ($module != 'hosting_site') {
      $func = $module . '_drush_context_import';
      $func(d(), $node);
    }
  }
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
