<?php
/**
 * @file
 * Expose the platform feature to hostmaster.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_platform_hosting_feature() {
  $features = [];
  $features['platform'] = ['title' => t('Platform administration'), 'description' => t('Create and manage platforms.'), 'status' => HOSTING_FEATURE_REQUIRED, 'module' => 'hosting_platform', 'node' => 'platform', 'group' => 'required', 'role_permissions' => ['aegir platform manager' => ['administer platforms', 'create platform', 'delete platform', 'edit platform', 'view locked platforms', 'view platform', 'create sites on locked platforms'], 'aegir client' => ['view platform']]];
  return $features;
}
