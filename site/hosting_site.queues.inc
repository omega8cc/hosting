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
    ->fields('c', array('name', 'nid'))
    ->fields('p', array('git_root'))
    ->condition('n.status', NODE_PUBLISHED)
    ->condition('s.status', HOSTING_SITE_ENABLED)
    ->isNotNull('git_root')
    ->condition('git_root', '', '!=')
    ->execute()
    ->fetchAllAssoc('name');

  $hostmaster_path = d('@hostmaster')->root;
  $bin_path = realpath(dirname($hostmaster_path) . '/bin');
  foreach ($result as $name => $site) {
    $site_node = node_load($site->nid);
    $t = [
      '@name' => $site->name,
      '@git_root' => $site->git_root,
    ];

    if ($site_node->git_ref_type != 'branch') {
      $t['@reason'] = t('Not on a branch');
    }
    elseif (!isset($site_node->hosting_settings['deployment']['continuous'])) {
      $t['@reason'] = t('Continuous deployment is disabled.');
    }
    else {
      $t['@reason'] = 'unsure.';
    }

    if ($site_node->git_ref_type == 'branch' && ($site_node->hosting_settings['deployment']['continuous'] ?? false)) {
      drush_log(dt('Checking site @name at @git_root...', $t), 'notice');
      $command = "cd {$site_node->git_root} && $bin_path/git-behind && drush @hostmaster hosting-task @{$name} deploy &";
      drush_shell_proc_open($command);
    }
    else {
      drush_log(dt('Skipping deployment for site @name at @git_root because @reason', $t), 'notice');
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
