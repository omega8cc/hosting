<?php
/**
 * @file
 * Expose the Backdrop feature to hostmaster.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_backdrop_hosting_feature() {
  $features['backdrop'] = array(
    'title' => t('Backdrop CMS platforms'),
    'description' => t('Treat Backdrop CMS codebases as a first-class platform type: partition Backdrop and Drupal platforms at site creation and install Backdrop sites through the Backdrop path.'),
    // Ships disabled: enabled only where BOA opts in via _BACKDROP_SUPPORT.
    'status' => HOSTING_FEATURE_DISABLED,
    'module' => 'hosting_backdrop',
    'group' => 'experimental',
  );
  return $features;
}
