<?php

/**
 * @file
 * Drush include for the Hosting module.
 */

define('HOSTING_QUEUE_DEFAULT_LOCK_WAIT', 30);
// Both the dispatch and queue semaphores are held only for the brief fork-and-
// dispatch (hosting_tasks_queue forks each task and returns), so this is purely
// the recovery bound for a holder that died mid-dispatch. The old 3600s (1h)
// left a single crashed run blocking the queue for an hour; 900s (15m) is a
// generous margin over the real sub-minute hold while recovering far sooner.
define('HOSTING_QUEUE_LOCK_TIMEOUT', 900.0);
// Seconds a task runner waits for the instance's runner lock (one task runs
// at a time per hostmaster) before leaving its task queued for the next pass.
// Its own option, --runner-wait: --lock-wait means the queue semaphore above.
define('HOSTING_TASK_RUNNER_WAIT', 30);

/**
 * Implements hook_drush_command().
 */
function hosting_drush_command() {
  $items['hosting-dispatch'] = array(
    'description' => dt('Centralized command for dispatching the various queue processors (hosting, cron, backup etc.)'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'options' => array(
      'lock-wait' => dt('Time to wait to acquire a lock on dispatched queues. Defaults to @wait seconds.', array('@wait' => HOSTING_QUEUE_DEFAULT_LOCK_WAIT)),
      'force' => dt('Continue even if the task queue is still locked.'),
    ),
  );

  $items['hosting-setup'] = array(
    'description' => dt('Set up initial configuration settings such as the cron entry for the queue dispatcher and more.'),
  );

  // If we're trying to get help, then try to bootstrap as much as possible.
  $current_command = drush_get_command();
  if (isset($current_command['command']) && ($current_command['command'] == 'help')) {
    drush_bootstrap_max();
  }

  // If we've not bootstrapped fully, then this function may not be around.
  if (function_exists('hosting_get_queues')) {
    $queues = hosting_get_queues();
    foreach ($queues as $queue => $info) {
      $dispatch = dt("Dispatched: @items items every @frequency minutes", array('@items' => $info['items'], '@frequency' => round($info['frequency'] / 60)));
      $items['hosting-' . $queue] = array(
        'callback' => 'hosting_run_queue',
        'description' => $info['description'] . " " . $dispatch,
        'queue' => $queue,
        'options' => array(
          'lock-wait' => dt('Time to wait to acquire a lock on the @queue queue. Defaults to @wait seconds.', array('@queue' => $queue, '@wait' => HOSTING_QUEUE_DEFAULT_LOCK_WAIT)),
          'force' => dt('Continue even if the task queue is still locked.'),
          // Declared so a synchronous drain (--fork=0) passes Drush's strict
          // option check; the tasks queue reads it via drush_get_option('fork').
          'fork' => dt('Run each task in this process instead of forking it (--fork=0); default forks.'),
          'runner-wait' => dt('Seconds a synchronous tasks drain waits for a running task of this instance to finish before leaving the batch for the next pass (one task runs at a time). Defaults to @wait.', array('@wait' => HOSTING_TASK_RUNNER_WAIT)),
        ),
      );
    }
  }

  $items['hosting-runner-lock'] = array(
    'description' => dt('Show who holds this instance\'s task runner lock (one task runs at a time); with --release, kill that server connection so a hung runner stops blocking the queue.'),
    'options' => array(
      'release' => dt('Kill the holder\'s server connection and clear its record.'),
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );

  $items['hosting-task'] = array(
    'description' => 'execute a specific queue item',
    'arguments' => array(
      '@context_name' => 'Context to work on',
      'command' => 'provision-[command] to invoke',
      'task_args' => 'If triggering tasks by context_name, any additional command arguments are passed as task_args. Specify the needed task_args in the format name=value.',
    ),
    'options' => array(
      'force' => array(
        'description' => 'Force the specified task to execute even if it is not queued to run.',
      ),
      'runner-wait' => array(
        'description' => dt('Seconds to wait for a running task of this instance to finish before leaving this task queued for the next pass (one task runs at a time). Defaults to @wait; 0 does not wait.', array('@wait' => HOSTING_TASK_RUNNER_WAIT)),
      ),
    ),
    'examples' => array(
      'drush @hostmaster hosting-task @sitename.com backup "description=backup description"' => 'Run backup task on sitename.com with the description "backup description".',
      'drush @hostmaster hosting-task 42 --verbose --force' => 'Run the task with ID 42.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );

  $items['hosting-import'] = array(
    'description' => 'Import an existing backend context name into the front end.',
    'arguments' => array(
      '@context_name' => 'Context to import',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );

  $items['hosting-pause'] = array(
    'description' => dt('Prepare the hostmaster site to be migrated to a new platform.'),
  );

  $items['hosting-resume'] = array(
    'description' => dt('Complete the migration of the hostmaster site to a new platform.'),
    'arguments' => array(
      'example.com' => dt('The url of the site being migrated.')),
    'options' => array(
      'old_platform_name' => dt('The old platform name'),
      'new_platform_name' => dt('The new platform name'),
    ),
  );

  return $items;
}

/**
 * Command to import an existing provision named context.
 *
 * It generate nodes for it.
 *
 * @param string $alias
 *   The name of the provision context to import.
 */
function drush_hosting_import($alias) {
  if (!empty($alias)) {
    if (d($alias)->name) {
      drush_log("Importing $alias");
      hosting_drush_import($alias);
    }
  }
}

/**
 * Imports a drush named context / "site alias" into the hostmaster frontend.
 *
 * By creating nodes and translating the value.
 * This is bascally the reverse of the context_options hook.
 *
 * @param object|string $alias
 *   The name of the provision context to import.
 *
 * @return int
 *   The node ID associated with the alias.
 */
function hosting_drush_import($alias) {
  $name = (is_object($alias)) ? $alias->name : $alias;

  static $known_contexts = array();

  // Avoid re-importing the same object twice in one execution
  // of the script.
  if (isset($known_contexts[$name])) {
    drush_log("Already re-imported {$name} in this process.");
    return $known_contexts[$name];
  }

  $context = d($name);

  if ($node = hosting_context_load($name)) {
    drush_log("Context {$name} already has an associated node. Updating {$node->type} node {$node->nid} instead.");
    $node->no_verify = TRUE;
    $known_contexts[$name] = $node->nid;
  }
  else {
    // First time we've seen this context.
    $node = new stdClass();
    $node->type = $context->type;
    // Set the hosting name too.
    $node->hosting_name = trim($context->name, '@');
    $node->status = 1;
    $node->import = TRUE;
  }

  // Iterate through all the Drush commands which may want to save this node.
  $modules = drush_command_implements('drush_context_import');
  foreach ($modules as $module) {
    $func = "{$module}_drush_context_import";
    $func($context, $node);
  }

  node_save($node);
  if ($node->nid) {
    drush_log("Context {$name} has been imported. Updated {$node->type} node {$node->nid}.");
    $known_contexts[$name] = $node->nid;
  }

  return $node->nid;
}

/**
 * Show or release the instance's task runner lock.
 *
 * The lock is a MySQL named lock held by a running task's connection
 * (task/hosting_task.module). A runner that dies releases it; a runner that
 * hangs holds it, and after hosting_task_runner_lock_max_hold every launcher
 * runs beside it, loudly. This is the operator's hand on it: the holder's
 * connection id, the pid, task and time it recorded, and --release to kill
 * that connection (the hostmaster's own database user may kill its own
 * connections; nothing else on the server is touched).
 */
function drush_hosting_runner_lock() {
  if (!function_exists('hosting_task_runner_lock_holder')) {
    return drush_set_error('HOSTING_RUNNER_LOCK', dt('The hosting_task module is not available.'));
  }
  $name = hosting_task_runner_lock_name();
  $holder = hosting_task_runner_lock_holder();
  if (!$holder) {
    drush_log(dt('The task runner lock @name is free.', array('@name' => $name)), 'ok');
    return TRUE;
  }
  $record = hosting_task_runner_lock_holder_record($holder);
  $running = hosting_task_running_now();
  drush_log(dt('The task runner lock @name is held by server connection @conn@record@task.', array(
    '@name' => $name,
    '@conn' => $holder,
    '@record' => $record ? dt(' (recorded: pid @pid, task node/@nid, since @since, @age)', array('@pid' => $record['pid'], '@nid' => $record['nid'], '@since' => format_date($record['since'], 'short'), '@age' => format_interval(time() - $record['since']))) : dt(' (no record of this holder)'),
    '@task' => $running ? dt('; likely task node/@nid pid @pid', array('@nid' => $running->nid, '@pid' => $running->pid)) : '',
  )), 'ok');
  if (!drush_get_option('release', FALSE)) {
    return TRUE;
  }
  try {
    db_query("KILL " . (int) $holder);
  }
  catch (Exception $e) {
    return drush_set_error('HOSTING_RUNNER_LOCK', dt('Could not kill server connection @conn: @error', array('@conn' => $holder, '@error' => $e->getMessage())));
  }
  variable_del('hosting_task_runner_lock_holder');
  sleep(1);
  $after = hosting_task_runner_lock_holder();
  watchdog('hosting_task', 'The task runner lock was released by hand: server connection @conn killed@record.', array('@conn' => $holder, '@record' => $record ? dt(' (pid @pid, task node/@nid)', array('@pid' => $record['pid'], '@nid' => $record['nid'])) : ''), WATCHDOG_WARNING);
  drush_log(dt('Server connection @conn killed; the lock is now @state.', array('@conn' => $holder, '@state' => $after ? dt('held by connection @c', array('@c' => $after)) : dt('free'))), $after ? 'warning' : 'ok');
  return TRUE;
}
