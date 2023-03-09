<?php

/**
 * @file
 * Drush include for the Hosting module's dispatch command.
 */

/**
 * Main queue processing drush command for hostmaster.
 *
 * This is a single command, which will (based on configuration) run all the
 * other queue commands (cron, backup, tasks, stats). This is done so that there
 * is only one cron job to configure, and allow the frequency of calls to be
 * configured from the frontend interface.
 */
function drush_hosting_dispatch() {
  $now = REQUEST_TIME;
  variable_set("hosting_dispatch_last_run", $now);
  drush_log(dt("Dispatching queues."), 'ok');
  watchdog('hosting', t('Started hosting-dispatch command.'));
  $platform = node_load(HOSTING_OWN_PLATFORM);
  $root = $platform->publish_path;

  if (variable_get('hosting_dispatch_enabled', FALSE)) {
    $queues = hosting_get_queues();
    $results = [];
    foreach ($queues as $queue => $info) {
      $semaphore = "hosting_dispatch_{$queue}_running";
      $lock_wait = drush_get_option('lock-wait', HOSTING_QUEUE_DEFAULT_LOCK_WAIT);
      if (!lock_wait($semaphore, $lock_wait) || drush_get_option('force', FALSE)) {
        if (lock_acquire($semaphore, HOSTING_QUEUE_LOCK_TIMEOUT)) {
          drush_log(dt('Acquired lock on @queue queue.', array('@queue' => $queue)), 'notice');
        }
        elseif (drush_get_option('force', FALSE)) {
          drush_log(dt('Bypassing lock on @queue queue.', array('@queue' => $queue)), 'warning');
        }
        else {
          drush_die(dt('Cannot acquire lock on @queue queue.', array('@queue' => $queue)), 'warning');
        }
      }
      else {
        drush_die(dt("Cannot acquire lock on @queue queue after waiting @wait seconds. A longer wait time can be set with the --lock-wait option.", array('@queue' => $queue, '@wait' => $lock_wait)), 'warning');
      }

      if ($info['enabled']) {
        if (($now - $info["last"]) >= $info["calc_frequency"] || drush_get_option('force', FALSE)) {
          $count = $info['calc_items'] - $info['running_items'];
          if ($count <= 0) {
            $results[$queue] = 'full';
            drush_log(dt("Maximum number of tasks (@count) already running.", array('@count' => $info['running_items'])), 'notice');
          }
          else {
            $results[$queue] = 'run';
            drush_log(dt("Found @running running @item, starting @count out of @max items.",
                    array(
                      '@running' => $info['running_items'],
                      '@item' => $info['running_items'] > 1? $info['plural']: $info['singular'],
                      '@count' => $count,
                      '@max' => $info['calc_items'])
                      )
                    , 'ok');
            drush_invoke_process('@self', "hosting-" . $queue, array(), array('items' => $count, 'strict' => FALSE, 'integrate' => TRUE, 'dispatch-using-alias' => TRUE));
          }
        }
        else {
          $results[$queue] = 'early';
          drush_log(dt("Too early for queue @queue.", array('@queue' => $queue)), 'notice');
        }
      }
      else {
        $results[$queue] = 'disabled';
        drush_log(dt("Queue @queue disabled.", array('@queue' => $queue)), 'notice');
      }

      drush_log(dt('Releasing @queue lock.', array('@queue' => $queue)), 'notice');
      lock_release($semaphore);
    }

    watchdog('hosting', 'Hosting dispatch complete. Queues: %results', [
      '%results' => print_r($results, 1)
    ]);
  }
  else {
    drush_log(dt("Dispatching disabled."));
  }
}
