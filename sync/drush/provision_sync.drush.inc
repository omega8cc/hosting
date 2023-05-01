<?php

/**
 * @file
 * Drush commands for Provision Sync
 */

/**
 * Implementation of hook_drush_command().
 * Provides provision commands.
 */
function provision_sync_drush_command() {
  $items = array();
  $items['provision-sync'] = array(
    'description' => 'Sync database and files from a chosen source site.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'drush dependencies' => array('sql', 'core'),
    'options' => array(
      'backup' => 'Create a backup of the destination before sync.',
      'database' => 'Sync site database.',
      'files' => 'Sync site files.',
      'deploy' => 'Create deploy task after sync.',
    ),
    'arguments' => array(
      'from' => 'Name of subdirectory within /sites or a site-alias.',
      'to' => 'Name of subdirectory within /sites or a site-alias.',
    ),
    'examples' => array(
      'drush provision-sync @prod @dev' => 'Copies the database and files from @prod to @dev',
    ),
  );
  return $items;
}

/**
 * Includes the sync.provision.inc file.
 */
function provision_sync_command_include() {
  include_once(dirname(__FILE__) . '/sync.provision.inc');
}
