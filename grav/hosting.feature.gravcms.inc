<?php
/**
 * @file
 * Expose the Grav feature to hostmaster.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_grav_hosting_feature() {
  // The feature key must NOT be 'grav': a disabled feature's key is written
  // verbatim into the Aegir-wide drushrc $options['exclude'] (see
  // Provision_Config_Drushrc_Aegir), which Drush 8 merges into its
  // commandfile-discovery basename blacklist — the key 'grav' would skip the
  // .drush/sys/provision/grav/ engine layer on every backend Drush call
  // whenever the feature is off, silently routing existing Grav platforms
  // through the Drupal paths (the D-023 lesson).
  $features['gravcms'] = array(
    'title' => t('Grav CMS platforms'),
    'description' => t('Treat Grav 2 codebases as a first-class platform type: partition Grav and Drupal platforms at site creation, install Grav site capsules through the Grav path, and provide the no-database server type flat-file sites attach to.'),
    // Ships disabled: enabled only where BOA opts in via _GRAV_SUPPORT.
    'status' => HOSTING_FEATURE_DISABLED,
    'module' => 'hosting_grav',
    'group' => 'experimental',
  );
  return $features;
}
