<?php

/**
 * @file
 * Drush include for the Hosting module's hosting pause command.
 */

/**
 * Make sure Aegir is ready to be upgraded.
 */
function drush_hosting_pause_validate() {
  // Wait for the task queue to complete.
  while (hosting_task_count_running() > 0) {
    // Runners that died without their shutdown handler leave PROCESSING
    // rows no process will ever finish; without this reap the wait above
    // spins on corpses for up to the whole running-items window (8h) and
    // stalls every hostmaster upgrade behind it.
    if (function_exists('hosting_task_reap_orphaned_tasks') && hosting_task_reap_orphaned_tasks()) {
      continue;
    }
    drush_log(dt('Waiting for the task queue to be processed and tasks to complete.'), 'notice');
    sleep(1);
  }
}

/**
 * Drush command to pause the Aegir frontend queues.
 *
 * This is really just deleting our code from the crontab.
 */
function drush_hosting_pause() {
  // Wipe out cron entry.
  _hosting_setup_cron($add = FALSE);
}
