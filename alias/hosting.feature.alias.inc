<?php
/**
 * @file
 * Expose the alias feature to hostmaster.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_alias_hosting_feature() {
  $features = [];
  $features['alias'] = ['title' => t('Site aliasing'), 'description' => t('Provide multiple domain names for sites to be accessible by.'), 'status' => HOSTING_FEATURE_ENABLED, 'module' => 'hosting_alias', 'group' => 'optional', 'role_permissions' => ['aegir client' => ['create site aliases']]];
  return $features;
}
