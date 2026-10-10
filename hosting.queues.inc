<?php

/**
 * @file
 *   This file defines an API for defining new queues.
 */

/**
 * @defgroup backend-frontend-IPC Frontend/Backend Inter-process Communication
 * @{
 * Aegir is based on a very important distinction between the
 * "frontend" and the "backend". The frontend is the Drupal website
 * that creates the user interface that people manipulate. The backend
 * is (usually) a set a drush commands that do the actual operations
 * on the backend objects (servers, platforms, sites).
 *
 * The frontend and backend run as different users: the frontend runs
 * under the webserver user, while the backend run as a separate UNIX
 * user, which we call the backend sandbox. This distinction is
 * important as it brings an extra layer of security. It also allows
 * for commands to be ran on different servers more easily.
 *
 * The way the frontend communicates to the backend is through the
 * use of "queues", that are actually defined in the frontend
 * (hosting). The frontend also define drush commands that are ran
 * through a cronjob and therefore provide the glue to the backend.
 *
 * The point of entry is through the "dispatcher" (the
 * hosting-dispatch command), which runs the different queues
 * appropriately. There are different types of queues (see
 * hook_hosting_queues()), but the most important one is the "task"
 * queue, which runs the basic tasks like install and backup.
 *
 * When tasks are ran, a provision command is called with the same
 * name of the task type defined in the frontend (see
 * drush_hosting_task()). If we are running a special task like
 * install, verify or import, data from the task is actually saved in
 * the backend, through the backend provision-save command. The data
 * saved is from the associative array "context_options", defined from
 * the hook see hook_hosting_TASK_OBJECT_context_options(). A good
 * example of such a variable is the site name or platform it is on,
 * which are obviously saved in the backend.
 *
 * There is also an "options" array that are just one-time parameters
 * that are not stored in the backend context. A good example of that
 * is the site email address, that is just sent during the install or
 * password resets task but not saved.
 */

/**
 * Retrieve a list of queues that need to be dispatched
 *
 * Generate a list of queues, and the frequency / amount of items
 * that need to be processed for each of them.
 */
function hosting_get_queues($refresh = FALSE) {
  static $cache = NULL;

  if (is_null($cache) || $refresh) {
    $cache = array();
    $defaults = array(
      'type' => HOSTING_QUEUE_TYPE_SERIAL,
      'max_threads' => 6,
      'threshold' => '100',
      'min_threads' => 1,
      'timeout' => strtotime("10 minutes", 0),
      'frequency' => strtotime("5 minutes", 0),
      'items' => 5,
      'enabled' => TRUE,
      'singular' => t('item'),
      'plural' => t('items'),
    );
    $queues = module_invoke_all('hosting_queues');
    if (!is_array($queues)) {
      $queues = array();
    }
    drupal_alter('hosting_queues', $queues);
    foreach ($queues as $key => $queue) {
      $queue = array_merge($defaults, $queue);

      // Configurable settings.
      $configured = array(
        'frequency' => variable_get('hosting_queue_' . $key . '_frequency', $queue['frequency']),
        'items' => variable_get('hosting_queue_' . $key . '_items', $queue['items']),
        'enabled' => variable_get('hosting_queue_' . $key . '_enabled', $queue['enabled']),
        'last_run' => hosting_queue_last_run($key),
        'interval' => variable_get('hosting_queue_' . $key . '_interval', FALSE),
      );
      $queue = array_merge($queue, $configured);

      if ($queue['type'] == HOSTING_QUEUE_TYPE_BATCH) {
        $threads = $queue['total_items'] / $queue['threshold'];
        if ($threads <= $queue['min_threads']) {
          $threads = $queue['min_threads'];
        }
        elseif ($threads > $queue['max_threads']) {
          $threads = $queue['max_threads'];
        }
        $queue['calc_threads'] = $threads;
        $queue['calc_frequency'] = ceil($queue['frequency'] / $threads);
        $queue['calc_items'] = ceil($queue['total_items'] / $threads);
      }
      elseif ($queue['type'] == HOSTING_QUEUE_TYPE_SPREAD) {
        // If there are 0 items in the queue avoid a division by 0.
        if ($queue['total_items'] > 0) {
          // Process the queue as often as is needed to get through all the
          // items once per frequency, but at most once a minute since the
          // default dispatcher runs that quickly.
          $queue['calc_frequency'] = max(60, $queue['frequency'] / $queue['total_items']);
          // Compute the number of items to process per invocation. Usually this is will be 1.
          $queue['calc_items'] = ceil($queue['calc_frequency'] / $queue['frequency'] * $queue['total_items']);
        }
        else {
          // There are 0 items in the queue, process 0 tasks once a day anyway.
          $queue['calc_frequency'] = 86400;
          $queue['calc_items'] = 0;
        }
        // Pretend like we do not have any previously run items still processing.
        $queue['running_items'] = 0;
      }
      else {
        $queue['calc_frequency'] = $queue['frequency'];
        $queue['calc_items'] = $queue['items'];
      }

      $queue['last'] = (int) hosting_queue_last_run($key);
      $queues[$key] = $queue;
    }
    // Allow altering the processed queue data.
    drupal_alter('hosting_processed_queues', $queues);
    $cache = $queues;
  }

  return $cache;
}

/**
 * When a queue (or the dispatcher, as 'dispatch') last ran.
 *
 * Read from the hosting_queue_run table on every call, never cached: the
 * dispatcher re-reads it under its semaphore to see a sibling's stamp.
 *
 * @param string $queue
 *   The queue name.
 *
 * @return int|false
 *   The timestamp, or FALSE when the queue never ran.
 */
function hosting_queue_last_run($queue) {
  if (!hosting_queue_run_table_exists()) {
    // Before update 7306: the variable, read from its table rather than from
    // this process's bootstrap copy, so a sibling dispatcher's stamp is seen.
    $raw = db_query("SELECT value FROM {variable} WHERE name = :name", array(':name' => 'hosting_queue_' . $queue . '_last_run'))->fetchField();
    return $raw === FALSE ? FALSE : (int) @unserialize($raw);
  }
  $value = db_query("SELECT last_run FROM {hosting_queue_run} WHERE queue = :queue", array(':queue' => $queue))->fetchField();
  return $value === FALSE ? FALSE : (int) $value;
}

/**
 * Stamp a queue's (or the dispatcher's) last run.
 *
 * A row write, not a variable write: variable_set() clears the bootstrap
 * variable cache, and the dispatcher stamped three variables a minute.
 *
 * @param string $queue
 *   The queue name, or 'dispatch'.
 * @param int|null $time
 *   The timestamp; the request time when NULL.
 */
function hosting_queue_set_last_run($queue, $time = NULL) {
  $time = ($time === NULL) ? REQUEST_TIME : (int) $time;
  if (!hosting_queue_run_table_exists()) {
    variable_set('hosting_queue_' . $queue . '_last_run', $time);
    return;
  }
  db_merge('hosting_queue_run')
    ->key(array('queue' => $queue))
    ->fields(array('last_run' => $time))
    ->execute();
}

/**
 * Whether the hosting_queue_run table exists yet (update 7306 creates it).
 */
function hosting_queue_run_table_exists() {
  $exists = &drupal_static(__FUNCTION__);
  if (!isset($exists)) {
    $exists = db_table_exists('hosting_queue_run');
  }
  return $exists;
}

/**
 * Run a queue specified by hook_hosting_queues()
 *
 * Run an instance of a queue processor. This function contains all the book keeping
 * functionality needed to ensure that the queues are running as scheduled.
 */
function hosting_run_queue() {
  $cmd = drush_get_command();
  $queue = $cmd['queue'];
  $count = drush_get_option(array('i', 'items'), 5); // process a default of 5 items at a time.
  $semaphore = "hosting_queue_{$queue}_running";

  hosting_queue_set_last_run($queue);

  drush_log(dt('Acquiring lock on @queue queue.', array('@queue' => $queue)));
  $lock_wait = drush_get_option('lock-wait', HOSTING_QUEUE_DEFAULT_LOCK_WAIT);
  if (!lock_wait($semaphore, $lock_wait) || drush_get_option('force', FALSE)) {
    if (lock_acquire($semaphore, HOSTING_QUEUE_LOCK_TIMEOUT)) {
      drush_log(dt('Acquired lock on @queue queue.', array('@queue' => $queue)));
    }
  }
  elseif (drush_get_option('force', FALSE)) {
    drush_log(dt('Bypassing lock on @queue queue.', array('@queue' => $queue)), 'warning');
  }
  else {
    drush_die(dt('Cannot acquire lock on @queue queue.', array('@queue' => $queue)));
  }

  $func = "hosting_" . $queue . "_queue";

  if (function_exists($func)) {
    $func($count);
  }

  drush_log(dt('Releasing @queue lock.', array('@queue' => $queue)));
  lock_release($semaphore);
}

/**
 * Calculate the time at which the task queue will run next.
 *
 * There are two possibilities here - the task was never run, in which
 * case we compute the next time it was run.
 *
 * The second case is the case where the task wasn't run in the last
 * queue run even though it was scheduled, so we compute the next
 * iteration.
 *
 * @param $queue mixed
 *   the queue name or a queue array as returned by
 *   hosting_get_queues()
 *
 * @return integer
 *   the date at which the queue will run as a number of seconds since
 *   the UNIX epoch
 */
function _hosting_queue_next_run($queue) {
  if (!is_array($queue)) {
    $queues = hosting_get_queues();
    $queue = $queues[$queue];
  }
  $freq = $queue['frequency'];
  $last = $queue['last_run'];
  $now = REQUEST_TIME;
  return max($last + $freq,
    ( $now - ( ($now - $last) % $freq ) ) + $freq );
}

function _hosting_queues_clean_output($return) {
  return filter_xss($return, array());
}


function _hosting_dispatch_cmd() {
  $cmd = sprintf("/usr/bin/env php %s %s hosting-dispatch ", DRUSH_COMMAND, escapeshellarg(d()->name));
  return $cmd;
}

/**
 * Generate a crontab entry.
 *
 * @return string
 *   The lines to be added.
 *
 * @see hosting_queues_cron_removal()
 */
function hosting_queues_cron_cmd() {
  $command = _hosting_dispatch_cmd();
  $return = <<<END
SHELL=/bin/sh
PATH=$_SERVER[PATH]
* * * * * $command
END;
  return $return;
}

/**
 * Remove Aegir lines from a crontab.
 *
 * @param array $crontab
 *   Multi line array of the current crontab entry.
 *
 * @return array
 *   The left over non-Aegir lines.
 *
 * @see hosting_queues_cron_cmd()
 */
function hosting_queues_cron_removal($crontab) {

  foreach ($crontab as $k => $cron) {
    if (preg_match('|SHELL=/bin/sh|', $cron)) {
      unset($crontab[$k]);
    }
    if (preg_match("|PATH=$_SERVER[PATH]|", $cron)) {
      unset($crontab[$k]);
    }
    if (preg_match('|hosting-dispatch|', $cron)) {
      unset($crontab[$k]);
    }
  }

  return $crontab;
}

/**
 * @} End of "defgroup backend-frontend-IPC".
 */
