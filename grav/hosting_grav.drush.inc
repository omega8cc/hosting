<?php
/**
 * @file
 * Drush hooks for hosting_grav.
 *
 * PHP 5.6 floor: this file is loaded under the Drush-8 backend on every box
 * that carries the hosting tree.
 */

/**
 * Implements drush_hook_pre_hosting_task().
 *
 * A Grav capsule has no database, and the copy a clone (or a migrate) makes
 * keeps the source's "none" database server whatever the task says: the
 * backend pins it and says so, but the FRONT END registers the copy from the
 * task arguments (hosting_clone_post_hosting_clone_task() copies
 * task_args['new_db_server'] onto the new node when it differs). The confirm
 * dialog already pins the value for a Grav site; this is the belt for a task
 * queued without the dialog (hosting_add_task(), the API, a script), so the
 * copy's node and the copy's backend context never disagree about the server.
 *
 * Runs after hosting_clone's own pre hook (commandfiles are invoked in name
 * order) on the shared HOSTING_TASK context, so both the option the backend
 * receives and the argument the post-task reads are normalised.
 */
function drush_hosting_grav_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if (!isset($task->ref) || !isset($task->ref->type) || $task->ref->type != 'site') {
    return;
  }
  if ($task->task_type != 'clone' && $task->task_type != 'migrate') {
    return;
  }
  if (!isset($task->ref->platform) || !hosting_grav_platform_is_grav($task->ref->platform)) {
    return;
  }
  if (isset($task->task_args['new_db_server']) && $task->task_args['new_db_server'] != $task->ref->db_server) {
    drush_log(dt('The database server @nid requested for the copy is ignored: a Grav capsule has no database and keeps the source\'s server.',
      array('@nid' => $task->task_args['new_db_server'])), 'ok');
  }
  $task->task_args['new_db_server'] = $task->ref->db_server;
  unset($task->options['new_db_server']);
}
