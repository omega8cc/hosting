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
    'frequency' => strtotime("1 minute", 0), # run queue every minute.
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
    ->condition('git_root', '', '!=')
    ->execute()
    ->fetchAllKeyed();

  $hostmaster_path = d('@hostmaster')->root;
  $bin_path = realpath(dirname($hostmaster_path) . '/bin');
  drush_log(format_plural(count($result), dt('Checking git remotes on 1 site...'), dt('Checking git remotes on @count sites...')), 'notice');
  foreach ($result as $name => $path) {
    if ($path && file_exists($path)) {
      $command = "cd $path && $bin_path/git-behind && drush @hostmaster hosting-task @{$name} deploy &";
      drush_shell_proc_open($command);  
    }
  }
}

/**
 * Implements hook_hosting_queued_execute()
 *
 * Runs in the hosting-queue-runner loop.
 *
 *
 * Runs way too fast. needs work. 
 * @see drush_hosting_queued()
 */
//function hosting_site_hosting_queued_execute() {
//  hosting_git_queue();
//}
