<?php
/**
 * @file
 * API for mapping packages to various Hosting node types
 */

/**
 * Generate instances to reference nodes to releases.
 *
 * This function uses extensible parameters, so you
 * can pass multiple groups of packages to reference
 * to the node.
 *
 * This mimics Drupal's module and theme override
 * functionality, in that only the top most item
 * will be referenced to the node.
 *
 * @param $rid
 *   The nid of the entity on which we are operating.
 * @param $type
 *   The type of the entity on which we are operating (platform, site or
 *   profile.
 */
function hosting_package_instance_sync($rid, $type) {
  $map = _hosting_package_plural_map();
  // phpcs:ignore PHPCompatibility.FunctionUse.ArgumentFunctionsReportCurrentValue.NeedsInspection
  $args = func_get_args();

  $rid = array_shift($args);
  // We need to pass in $type, since there's a possibility of collision b/w a
  // profile's iid, and the nid of a site or platform.
  $type = array_shift($args);

  // Figure out the platform nid
  $platform = 0;
  if ($type == 'platform') {
    $platform = $rid;
  }
  elseif ($type == 'site') {
    $node = node_load($rid);
    $platform = $node->platform;
  }
  elseif ($type == 'profile') {
    $sql = 'SELECT rid
            FROM {hosting_package_instance}
            WHERE iid = :rid';
    $platform = db_query($sql, array(':rid' => $rid))->fetchField();
  }
  else {
    watchdog('hosting_package', 'Unrecognized entity type (' . $type . ') passed to hosting_package_instance_sync().', array(), WATCHDOG_WARNING);
  }

  // Mark every instance of this entity; the ones the sync does not see
  // again are removed at the end. A profile's instances are keyed by the
  // profile instance's own iid, which can equal the nid of a site or a
  // platform, so every sync stays within its own platform: a platform's and
  // a profile's rows name the platform, a site's rows name the site's
  // platform or -1 for site-specific packages; rows written before the
  // platform column existed carry 0.
  $scope = NULL;
  if ($platform) {
    $scope = ($type == 'site') ? array(0, -1, $platform) : array(0, $platform);
  }
  $mark = db_update('hosting_package_instance')
    ->fields(array(
      'status' => -1, // @todo remove magic number?
    ))
    ->condition('rid', $rid);
  if ($scope) {
    $mark->condition('platform', $scope, 'IN');
  }
  $mark->execute();

  foreach ($map as $plural => $key) {
    $merged = array();
    foreach ($args as $index => $arg) {
      if (is_array($args[$index]) && array_key_exists($plural, $args[$index])) {
        $merged = array_merge($merged, $args[$index][$plural]);
      }
    }

    foreach ($merged as $name => $package) {
      $conditions = array(
        'i.rid' => $rid,
        'i.package_id' => $package['package_id'],
      );
      if ($scope) {
        $conditions['i.platform'] = $scope;
      }
      $instance = hosting_package_instance_load($conditions);
      if (!$instance) {
        $instance = new stdClass();
        $instance->rid = $rid;
        $instance->package_id = $package['package_id'];
      }
      $instance->languages = isset($package['info']['languages']) ? $package['info']['languages'] : array();
      $instance->filename = isset($package['filename']) ? $package['filename'] : '';
      $instance->version = isset($package['version']) ? $package['version'] : 'Unknown';
      $instance->schema_version = isset($package['schema_version']) ? $package['schema_version'] : 0;
      $instance->version_code = hosting_package_instance_version_code($instance->version);
      $instance->status = array_key_exists('status', $package) ? $package['status'] : 0;
      $instance->platform = isset($package['platform']) ? $package['platform'] : $platform;

      hosting_package_instance_save($instance);
    }
  }

  // Whatever is still marked is gone from the codebase. A platform's stale
  // rows include its vanished profile instances, and a profile's packages
  // are keyed by that instance's iid: nothing would ever look those rows up
  // again, and they were the bulk of a long-lived master's instance table.
  $stale = db_select('hosting_package_instance', 'i')
    ->fields('i', array('iid'))
    ->condition('rid', $rid)
    ->condition('status', -1); // @todo remove magic number?
  if ($scope) {
    $stale->condition('platform', $scope, 'IN');
  }
  $stale = $stale->execute()->fetchCol();
  if (!empty($stale)) {
    if ($type == 'platform') {
      hosting_package_instances_delete(hosting_package_instance_children($stale, $platform));
    }
    hosting_package_instances_delete($stale);
  }
}

/**
 * Delete package instances together with their language rows.
 *
 * @param array $iids
 *   Instance ids.
 *
 * @return int
 *   The number of instances deleted.
 */
function hosting_package_instances_delete($iids) {
  $iids = array_values(array_unique(array_filter(array_map('intval', (array) $iids))));
  if (empty($iids)) {
    return 0;
  }
  foreach (array_chunk($iids, 500) as $chunk) {
    db_delete('hosting_package_languages')
      ->condition('iid', $chunk, 'IN')
      ->execute();
    db_delete('hosting_package_instance')
      ->condition('iid', $chunk, 'IN')
      ->execute();
  }
  // Clear the static cache for languages of package instances.
  _hosting_package_instances_load_languages(NULL, TRUE);
  return count($iids);
}

/**
 * The instances keyed by the given profile instances.
 *
 * A profile's packages are recorded with rid = the profile instance's iid.
 * An iid can equal a node nid, so only rows that name the same platform (or
 * none, for rows older than the platform column) and whose rid is not a
 * site or platform nid count: those are the only nodes that own rows here.
 *
 * @param array $iids
 *   Profile instance ids.
 * @param int $platform
 *   The platform nid the profiles belong to.
 *
 * @return array
 *   Instance ids.
 */
function hosting_package_instance_children($iids, $platform) {
  $iids = array_values(array_unique(array_filter(array_map('intval', (array) $iids))));
  $platform = (int) $platform;
  if (empty($iids) || $platform <= 0) {
    return array();
  }
  return db_query("SELECT i.iid
                   FROM {hosting_package_instance} i
                   LEFT JOIN {node} n ON n.nid = i.rid AND n.type IN ('site', 'platform')
                   WHERE i.rid IN (:iids) AND i.platform IN (:platforms) AND n.nid IS NULL",
                   array(':iids' => $iids, ':platforms' => array(0, $platform)))->fetchCol();
}

/**
 * Delete every package instance recorded for a platform.
 *
 * Covers the platform's own rows and the profile children keyed by its
 * profile instances' iids. A live site's rows are never touched: a platform
 * cannot be deleted while it carries sites, and the guard costs nothing.
 *
 * @param int $platform
 *   The platform nid.
 *
 * @return int
 *   The number of instances deleted.
 */
function hosting_package_instances_delete_by_platform($platform) {
  $platform = (int) $platform;
  if ($platform <= 0) {
    return 0;
  }
  $iids = db_query("SELECT i.iid
                    FROM {hosting_package_instance} i
                    WHERE i.platform = :platform
                    AND i.rid NOT IN (SELECT s.nid FROM {hosting_site} s INNER JOIN {node} n ON n.vid = s.vid WHERE s.status <> :deleted)",
                    array(':platform' => $platform, ':deleted' => _hosting_package_deleted_status('site')))->fetchCol();
  return hosting_package_instances_delete($iids);
}

/**
 * Whether any package instance belongs to no live platform or site.
 */
function hosting_package_instances_gc_pending() {
  return (bool) db_query_range(_hosting_package_instances_gc_select('1'), 0, 1, _hosting_package_instances_gc_args())->fetchField();
}

/**
 * Collect package instances that belong to no live platform or site.
 *
 * The rows of a deleted platform used to survive it: its profile children
 * were keyed by instance iids nobody looked up again, and a long-lived
 * master carried hundreds of thousands of them. Site-specific packages
 * (platform -1) and rows written before the platform column existed (0)
 * are left alone; a live site's rows are never touched. Bounded so a queue
 * run never holds the table.
 *
 * @param int $limit
 *   The most instances to delete in one call.
 *
 * @return int
 *   The number of instances deleted.
 */
function hosting_package_instances_gc($limit = 2000) {
  $limit = max(1, (int) $limit);
  $iids = db_query_range(_hosting_package_instances_gc_select('i.iid'), 0, $limit, _hosting_package_instances_gc_args())->fetchCol();
  $deleted = hosting_package_instances_delete($iids);

  // Language rows whose instance is gone.
  $orphans = db_query_range("SELECT DISTINCT l.iid
                             FROM {hosting_package_languages} l
                             LEFT JOIN {hosting_package_instance} i ON i.iid = l.iid
                             WHERE i.iid IS NULL", 0, $limit)->fetchCol();
  if (!empty($orphans)) {
    db_delete('hosting_package_languages')
      ->condition('iid', $orphans, 'IN')
      ->execute();
  }
  return $deleted;
}

/**
 * The SELECT of the leftover instances; see hosting_package_instances_gc().
 */
function _hosting_package_instances_gc_select($fields) {
  return "SELECT " . $fields . "
          FROM {hosting_package_instance} i
          WHERE i.platform > 0
          AND i.platform NOT IN (SELECT p.nid FROM {hosting_platform} p INNER JOIN {node} n ON n.vid = p.vid WHERE p.status <> :pdeleted)
          AND i.rid NOT IN (SELECT s.nid FROM {hosting_site} s INNER JOIN {node} m ON m.vid = s.vid WHERE s.status <> :sdeleted)";
}

/**
 * The arguments of _hosting_package_instances_gc_select().
 */
function _hosting_package_instances_gc_args() {
  return array(
    ':pdeleted' => _hosting_package_deleted_status('platform'),
    ':sdeleted' => _hosting_package_deleted_status('site'),
  );
}

/**
 * The status value that marks a deleted site or platform.
 *
 * The constants live in the site and platform modules, which may not be
 * loaded when this include runs on its own.
 */
function _hosting_package_deleted_status($type) {
  if ($type == 'platform' && defined('HOSTING_PLATFORM_DELETED')) {
    return HOSTING_PLATFORM_DELETED;
  }
  if ($type == 'site' && defined('HOSTING_SITE_DELETED')) {
    return HOSTING_SITE_DELETED;
  }
  return -2;
}

function _hosting_package_instance_languages($instance) {
  if (isset($instance->languages) && count($instance->languages)) {
    db_delete('hosting_package_languages')
      ->condition('iid', $instance->iid)
      ->execute();
    foreach ($instance->languages as $language) {
      $id = db_insert('hosting_package_languages')
        ->fields(array(
          'iid' => $instance->iid,
          'language' => $language,
        ))
        ->execute();
    }
  }

  // Clear the static cache for languages of package instances.
  _hosting_package_instances_load_languages(NULL, TRUE);
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_package_instance_update(&$instance) {
  $id = db_update('hosting_package_instance')
    ->fields(array(
      'rid' => $instance->rid,
      'package_id' => $instance->package_id,
      'filename' => $instance->filename,
      'schema_version' => $instance->schema_version,
      'version' => $instance->version,
      'version_code' => $instance->version_code,
      'status' => $instance->status,
      'platform' => $instance->platform
    ))
    ->condition('iid', $instance->iid)
    ->execute();

  _hosting_package_instance_languages($instance);
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_package_instance_create(&$instance) {
  $id = db_insert('hosting_package_instance')
    ->fields(array(
      'rid' => $instance->rid,
      'package_id' => $instance->package_id,
      'filename' => $instance->filename,
      'schema_version' => $instance->schema_version,
      'version' => $instance->version,
      'version_code' => $instance->version_code,
      'status' => $instance->status,
      'platform' => $instance->platform
    ))
    ->execute();
  $instance->iid = $id;
  _hosting_package_instance_languages($instance);
}

/**
 * Get the first package instance object that maches given parameters.
 *
 * @see hosting_package_instances_load()
 */
function hosting_package_instance_load($param) {
  return _hosting_package_instances_load($param);
}

/**
 * Get an array of all package instance objects that match given parameters.
 *
 * @param mixed $param
 *   If $param is an integer it will be matched against an instance iid. If
 *   $param is an array then the key/value pairs will be used as a WHERE clause
 *   in the form "WHERE key0 = value0 AND ... AND keyN = valueN". Both the keys
 *   and values of the $param array must be strings.
 *
 * @return mixed
 *   Returns an array of instance objects keyed by the instance iid. If there
 *   are no matches an empty array will be returned. If there was an error FALSE
 *   will be returned.
 */
function hosting_package_instances_load($param) {
  return _hosting_package_instances_load($param, TRUE);
}

/**
 * Internal function for hosting_package_instance(s)_load.
 */
function _hosting_package_instances_load($param, $multiple = FALSE) {
  $instances = array();

  $query = db_select('hosting_package_instance', 'i')
    ->addTag('hosting_package_instances_load')
    ->fields('i', array('iid', 'version', 'version_code', 'schema_version', 'status', 'rid', 'package_id'));
  $package_alias = $query->leftJoin('hosting_package', 'p', 'i.package_id = %alias.nid');
  $query->fields($package_alias, array('package_type', 'nid', 'description', 'short_name', 'old_short_name'));
  $node_package_alias = $query->leftJoin('node', 'n', $package_alias . '.nid = %alias.nid');
  $query->fields($node_package_alias, array('title'));
  $node_instance_alias = $query->leftJoin('node', 'r', 'i.rid = %alias.nid');
  $query->fields($node_instance_alias, array('type'));
  $platform_alias = $query->leftJoin('hosting_platform', 'h', $node_instance_alias . '.nid = %alias.nid');

  if (is_numeric($param)) {
    $query->condition('i.iid', $param);
  }
  elseif (is_array($param)) {
    // Turn the conditions into a query.
    foreach ($param as $key => $value) {
      $query->condition($key, $value);
    }
  }
  else {
    return FALSE;
  }

  $result = $query->execute();

  foreach ($result as $instance) {
    $instance->languages = _hosting_package_instances_load_languages($instance->iid);
    if (!$multiple) {
      return $instance;
    }
    $instances[$instance->iid] = $instance;
  }
  return $instances;
}

/**
 * Get the languages associated with an instance of a package.
 */
function _hosting_package_instances_load_languages($instance_id = NULL, $reset = FALSE) {
  static $instances = array();
  if (!empty($reset)) {
    $instances = array();
  }

  // Build the array of languages if requested and we need to.
  if (!is_null($instance_id) && !isset($instances[$instance_id])) {
    $languages = array('en' => 'en');

    // load language options: if argument is null, load all language options
    $lang_result = db_query("SELECT DISTINCT(language) FROM {hosting_package_languages} WHERE iid = :iid", array(':iid' => $instance_id));
    while ($language = $lang_result->fetch()) {
      $languages[$language->language] = _hosting_language_name($language->language);
    }

    $instances[$instance_id] = $languages;
  }

  // Return the array of languages if requested.
  return !is_null($instance_id) ? $instances[$instance_id] : NULL;

}


/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_package_instance_save(&$instance) {
  if (!isset($instance->iid)) {
    hosting_package_instance_create($instance);
  }
  else {
    hosting_package_instance_update($instance);
  }
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_package_instance_delete($instance) {
  db_delete('hosting_package_languages')
    ->condition('iid', $instance->iid)
    ->execute();
  db_delete('hosting_package_instance')
    ->condition('iid', $instance->iid)
    ->execute();
}

/**
 * Turn package version string into a number
 *
 * This function returns a float value for the number that represents the
 * version string.
 */
function hosting_package_instance_version_code($version) {

  $defaults = array(
    'platform_major' => 0,
    'platform_minor' => 0,
    'package_major' => 0,
    'package_minor' => 0,
    'package_version_type' => 'release',
    'package_patch_level' => 0,
  );

  $release_types = array(
    '' => 0,
    'dev' => 0,
    'unstable' => 1,
    'alpha' => 2,
    'beta' => 3,
    'rc' => 4,
    'release' => 5,
  );

  $regex = array(
    'core' => '/^(?P<platform_major>\d?)\.(?P<platform_minor>[x\d]*)?(-(?P<package_version_type>dev|unstable|alpha|beta|rc)?(?P<package_patch_level>\d)*)?$/',
    'contrib' => "/(?P<platform_major>\d?)\.(?P<platform_minor>[x\d]*)?(-(?P<package_major>\d*)?\.(?P<package_minor>[x\d]*)?(-(?P<package_version_type>alpha|unstable|beta|rc|dev)?(?P<package_patch_level>[\d]*)?)?)?/",
  );

  $matches = array();
  if (preg_match($regex['core'], $version, $matches)) {
    $matches = array_merge($defaults, $matches);
  }
  elseif (preg_match($regex['contrib'], $version, $matches)) {
    $matches = array_merge($defaults, $matches);
  }
  else {
    return 0;
  }
  // we use the float type because php generally has a maximum integer value to small to handle this value
  $result =  (float) sprintf("%d%02d%02d%02d%02d%02d",
    $matches['platform_major'], $matches['platform_minor'], $matches['package_major'],
    $matches['package_minor'], $release_types[$matches['package_version_type']],
    $matches['package_patch_level']);

  return $result;
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function hosting_package_comparison($current, $target) {

  $current_table = _hosting_package_temporary_table($current);
  $target_table = _hosting_package_temporary_table($target);

  $status = array();
  $result = db_query("SELECT count(c.nid) AS error
                      FROM $current_table c
                      LEFT JOIN $target_table t
                      ON c.nid=t.nid
                      WHERE (t.schema_version > 0) && (c.schema_version > t.schema_version)
                      AND c.status = :cstatus",
                      array(
                        ':cstatus' => 1,
                        )
                      );

  foreach ($result as $obj) {
    $status['error'] = $obj->error;
  }

  $result = db_query("SELECT COUNT(c.nid) AS missing
                      FROM $current_table c
                      LEFT JOIN $target_table t
                      ON c.nid=t.nid
                      WHERE t.nid IS NULL
                      AND c.status = :c_status",
                      array(
                        ':c_status' => 1,
                        )
                      );
  foreach ($result as $obj) {
    $status['missing'] = $obj->missing;
  }

  $result = db_query("SELECT COUNT(c.nid) as upgrade
                      FROM $current_table c
                      LEFT JOIN $target_table t
                      ON c.nid=t.nid
                      WHERE (c.version_code < t.version_code) OR (c.schema_version < t.schema_version)
                      AND c.status = :c_status",
                      array(
                        ':c_status' => 1,
                        )
                      );

  foreach ($result as $obj) {
    $status['upgrade'] = $obj->upgrade;
  }

  $result = db_query("SELECT count(c.nid) AS downgrade
                      FROM $current_table c
                      LEFT JOIN $target_table t
                      ON c.nid=t.nid
                      WHERE (c.version_code > t.version_code)");

  foreach ($result as $obj) {
    $status['downgrade'] = $obj->downgrade;
  }

  return $status;
}


function _hosting_package_temporary_table($ref) {
  static $names = array();

  if (!isset($names[$ref])) {
    // Generate a unique table name for this $ref.
    $names[$ref] = db_query_temporary("SELECT p.nid, p.short_name, p.old_short_name, i.version_code, i.schema_version, i.version, i.status
              FROM {hosting_package} p
              LEFT JOIN {hosting_package_instance} i
              ON p.nid = i.package_id
              WHERE i.rid = :rid and i.platform != :platform",
              array( ':rid' => $ref, ':platform' => HOSTING_PACKAGE_INSTANCE_EXCLUDE));

  }

  return $names[$ref];
}

/**
 * Helper to determine if a particular site has a module installed.
 */
function _hosting_package_site_has_module($node, $module) {
  $package = hosting_package_instance_load(array('rid' => $node->nid, 'p.short_name' => $module));
  if (!is_object($package)) {
    return FALSE;
  }
  return $package->status;
}
