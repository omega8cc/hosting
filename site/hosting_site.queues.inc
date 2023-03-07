<?php

/**
 * @file
 */

/**
 * Implements hook_hosting_queues().
 *
 * Return a list of queues that this module needs to manage.
 */
function hosting_site_hosting_queues() {
  $queue['git'] = array(
    'name' => t('Git deploy queue'),
    'description' => t('Watch repositories for changes and trigger deployments when new remote commits are available.'),
    'type' => 'serial', // Run queue sequentially. always with the same parameters.
    'frequency' => strtotime("1 minute", 0), # run queue every minute.
    'items' => 0,
    'total_items' => 0,
    'singular' => t('site'),
    'plural' => t('sites'),
//    'running_items' => hosting_task_count_running(),
  );
  return $queue;
}

/**
 * Implements the git queue.
 * @return void
 */
function hosting_git_queue() {

  // Load all sites with a git_remote.
  $query = db_select('hosting_site', 's');
  $query->join('hosting_context', 'c', 'c.nid = s.nid');
  $query->join('hosting_platform', 'p', 'p.nid = s.platform');
  $query->join('node', 'n', 'n.nid = s.nid');
  $result = $query
    ->fields('c', array('name'))
    ->fields('p', array('git_root'))
    ->condition('n.status', NODE_PUBLISHED)
    ->condition('s.status', HOSTING_SITE_ENABLED)
    ->isNotNull('git_root')
    ->execute()
    ->fetchAllKeyed();
  
  foreach ($result as $name => $path) {
    $command = "cd $path && TIMEOUT=55 wait-for git-behind && drush @hostmaster hosting-task @{$name} &";
    drush_log("Launching git monitor process in $path: $command", "ok");
    $result_code = drush_shell_proc_open($command);
  }
}