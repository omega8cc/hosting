<?php

/**
 * @file
 *   The hosting feature definition for hosting_git.
 */

/**
 * Implements hook_hosting_feature
 */
function hosting_git_hosting_feature() {
  $features['git'] = array(
    'title' => t('Git Tasks'),
    'description' => t('Provides tasks to create branches & tags, commit changes, and push code.'),
    'status' => HOSTING_FEATURE_DISABLED,
    'module' => 'hosting_git',
  );

  return $features;
}
