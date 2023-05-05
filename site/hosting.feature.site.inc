<?php
/**
 * @file
 * Expose the site feature to hostmaster.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_site_hosting_feature() {
  $features = [];
  $features['site'] = ['title' => t('Site administration'), 'description' => t('Create and manage sites.'), 'status' => HOSTING_FEATURE_REQUIRED, 'module' => 'hosting_site', 'node' => 'site', 'group' => 'required', 'role_permissions' => ['aegir platform manager' => ['administer sites', 'create site', 'delete site', 'edit site', 'view site'], 'aegir account manager' => ['view site'], 'aegir client' => ['create site', 'delete site', 'view site', 'edit site']]];
  return $features;
}
