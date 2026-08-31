<?php
/**
 * @file
 * Expose the Textpattern feature to hostmaster.
 */

/**
 * Implements hook_hosting_feature().
 */
function hosting_txp_hosting_feature() {
  // The feature key must NOT be 'txp': a disabled feature's key is written
  // verbatim into the Aegir-wide drushrc $options['exclude'] (see
  // Provision_Config_Drushrc_Aegir), which Drush 8 merges into its
  // commandfile-discovery basename blacklist — the key 'txp' would skip the
  // .drush/sys/provision/txp/ engine layer on every backend Drush call
  // whenever the feature is off, silently routing existing Textpattern
  // platforms through the Drupal paths (the D-023 lesson).
  $features['txpcms'] = array(
    'title' => t('Textpattern CMS platforms'),
    'description' => t('Treat Textpattern codebases as a first-class platform type: partition Textpattern and Drupal platforms at site creation, and install Textpattern sites as multisite entries through the TXP path.'),
    // Ships disabled: enabled only where BOA opts in via _TXP_SUPPORT.
    'status' => HOSTING_FEATURE_DISABLED,
    'module' => 'hosting_txp',
    'group' => 'experimental',
  );
  return $features;
}
