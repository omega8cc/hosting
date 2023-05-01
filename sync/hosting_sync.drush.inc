<?php

/**
 * @file hosting_sync.drush.inc
 * Keeping things organized.  All code that is really run via drush commands
 * will go here.
 */

/**
 * Implements drush_HOOK_pre_COMMAND()
 *
 * This runs for each tasks during the command
 *    drush @hostmaster hosting-tasks
 *
 * NOTE: This ONLY runs when being called from a hostmaster task.
 * This hook should ONLY be used to pass Options from a hostmaster task form to
 * the $task object, or if you don't need this functionality from the command
 * line.
 */
function drush_hosting_sync_pre_hosting_task() {
  $task = &drush_get_context('HOSTING_TASK');

  // Sync
  if ($task->ref->type == 'site' && $task->task_type == 'sync') {
    $task->args[] = $task->task_args['source'];
    $task->args[] = $task->task_args['destination'];
    $task->options['database'] = $task->task_args['database'];
    $task->options['files'] = $task->task_args['files'];
    $task->options['deploy'] = $task->task_args['deploy'];
    $task->options['backup'] = $task->task_args['backup'];
  }
}

/**
 * Implements hook_hosting_TASK_NAME_task().
 */
function hosting_sync_post_hosting_sync_task($task, $data) {
  if ($task->ref->type == 'site' && $task->task_args['backup']) {
    $task = clone $task;

    // Load the destination node
    //$destination = hosting_get_site_by_url(ltrim($task->task_args['destination'], '@'));

    // Record the backup created during sync.
    //$task->ref = $destination;
    $task->task_args['description'] = t('Before sync from %source', array('%source' => $task->task_args['source']));
    hosting_site_post_hosting_backup_task($task, $data);
  }
  if ($task->ref->type == 'site' && $task->task_args['deploy']) {
    $deploy_task = hosting_add_task($task->rid, 'deploy');
    drush_log(dt('Deploy task has been queued: !link', [
      '!link' => url("node/$deploy_task->nid", ['absolute' => true]),
    ]), 'p_ok');
  }
}

