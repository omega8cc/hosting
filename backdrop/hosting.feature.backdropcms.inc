<?php
/**
 * @file
 * Expose the Backdrop feature to hostmaster.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_backdrop_hosting_feature() {
  // The feature key must NOT be 'backdrop': a disabled feature's key is
  // written verbatim into the Aegir-wide drushrc $options['exclude'] (see
  // Provision_Config_Drushrc_Aegir), which Drush 8 merges into its
  // commandfile-discovery basename blacklist — the key 'backdrop' would skip
  // the .drush/sys/provision/backdrop/ engine and the .drush/usr/backdrop
  // extension on every backend Drush call whenever the feature is off,
  // silently routing existing Backdrop platforms through the Drupal paths.
  $features['backdropcms'] = array(
    'title' => t('Backdrop CMS platforms'),
    'description' => t('Treat Backdrop CMS codebases as a first-class platform type: partition Backdrop and Drupal platforms at site creation and install Backdrop sites through the Backdrop path.'),
    // Ships disabled: enabled only where BOA opts in via _BACKDROP_SUPPORT.
    'status' => HOSTING_FEATURE_DISABLED,
    'module' => 'hosting_backdrop',
    'group' => 'experimental',
  );
  return $features;
}
