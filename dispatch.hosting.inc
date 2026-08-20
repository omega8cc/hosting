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
  drush_log(dt("Dispatching queues."));
  $platform = node_load(HOSTING_OWN_PLATFORM);
  $root = $platform->publish_path;

  if (variable_get('hosting_dispatch_enabled', FALSE)) {
    $queues = hosting_get_queues();
    foreach ($queues as $queue => $info) {
      $semaphore = "hosting_dispatch_{$queue}_running";
      $lock_wait = drush_get_option('lock-wait', HOSTING_QUEUE_DEFAULT_LOCK_WAIT);
      if (!lock_wait($semaphore, $lock_wait) || drush_get_option('force', FALSE)) {
        if (lock_acquire($semaphore, HOSTING_QUEUE_LOCK_TIMEOUT)) {
          drush_log(dt('Acquired lock on @queue queue.', array('@queue' => $queue)));
        }
        elseif (drush_get_option('force', FALSE)) {
          drush_log(dt('Bypassing lock on @queue queue.', array('@queue' => $queue)), 'warning');
        }
        else {
          drush_die(dt('Cannot acquire lock on @queue queue.', array('@queue' => $queue)));
        }
      }
      else {
        drush_die(dt("Cannot acquire lock on @queue queue after waiting @wait seconds. A longer wait time can be set with the --lock-wait option.", array('@queue' => $queue, '@wait' => $lock_wait)));
      }

      if ($info['enabled']) {
        if (!isset($info['running_items'])) {
          $info['running_items'] = 0;
        }
        // Re-read last_run while we hold the dispatch semaphore. $info["last"] was
        // snapshotted by hosting_get_queues() BEFORE this lock, and the forked
        // hosting-<queue> only stamps last_run seconds later, so two dispatch runs
        // racing here both saw the stale timestamp, both passed the frequency gate,
        // and each forked a full batch -- up to 2x the configured limit. The
        // re-read plus the synchronous stamp below make this semaphore actually
        // serialize: a run behind it sees the fresh timestamp and is gated out.
        // Drupal 7 loads the whole variable table into $conf once at bootstrap, so
        // a sibling dispatcher's variable_set() is invisible to variable_get()
        // until we reload from the DB -- without this the re-read returns our own
        // stale bootstrap value and the serialization is a no-op (same idiom as
        // queued/hosting_queued.drush.inc).
        global $conf;
        $conf = variable_initialize();
        // For the tasks queue, first reap PROCESSING rows whose runner
        // process is gone: a runner that dies without its shutdown handler
        // (signal kill, code-tree swap during a hostmaster upgrade, host
        // reboot) never writes a final status, and each such orphan is
        // subtracted from the concurrency budget below for the whole
        // running-items window - with the default single-task limit the
        // queue wedges for eight hours per orphan. Then re-count under the
        // semaphore: the pre-lock snapshot from hosting_get_queues() misses
        // both a sibling's just-forked batch and anything the reap healed.
        if ($queue == 'tasks' && function_exists('hosting_task_reap_orphaned_tasks')) {
          $reaped = hosting_task_reap_orphaned_tasks();
          if ($reaped) {
            drush_log(dt('Reaped @count orphaned task(s) left in processing by dead runners.', array('@count' => $reaped)), 'warning');
          }
          $info['running_items'] = hosting_task_count_running();
        }
        $last = variable_get("hosting_queue_{$queue}_last_run", $info["last"]);
        if (($now - $last) >= $info["calc_frequency"] || drush_get_option('force', FALSE)) {
          $count = $info['calc_items'] - $info['running_items'];
          if ($count <= 0) {
            drush_log(dt("Maximum number of tasks (@count) already running.", array('@count' => $info['running_items'])));
          }
          else {
            // Stamp the run now, before forking, so a dispatch serialized behind
            // this semaphore is gated out instead of launching a second batch.
            variable_set("hosting_queue_{$queue}_last_run", $now);
            drush_log(dt("Found @running running tasks, starting @count out of @max items.",
                    array(
                      '@running' => $info['running_items'],
                      '@count' => $count,
                      '@max' => $info['calc_items'])
                      )
                    );
            drush_invoke_process('@self', "hosting-" . $queue, array(), array('items' => $count, 'strict' => FALSE), array('fork' => TRUE));
          }
        }
        else {
          drush_log(dt("Too early for queue @queue.", array('@queue' => $queue)));
        }
      }
      else {
        drush_log(dt("Queue @queue disabled.", array('@queue' => $queue)));
      }

      drush_log(dt('Releasing @queue lock.', array('@queue' => $queue)));
      lock_release($semaphore);
    }
  }
  else {
    drush_log(dt("Dispatching disabled."));
  }
}
