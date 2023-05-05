<?php
/**
 * @file
 * Expose the client feature to hostmaster.
 */

/**
 * @file hosting.feature.client.inc
 * Register this module as an Aegir feature.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_client_hosting_feature() {
  $features = [];
  $features['client'] = ['title' => t('Clients'), 'description' => t('Track and manage ownership of hosted sites, and access to platforms.'), 'status' => HOSTING_FEATURE_REQUIRED, 'module' => 'hosting_client', 'node' => 'client', 'group' => 'required', 'role_permissions' => ['aegir platform manager' => ['administer clients'], 'aegir account manager' => ['administer clients', 'create client', 'edit client users', 'view client', 'edit client uname'], 'aegir client' => ['edit own client', 'view client']]];
  return $features;
}
