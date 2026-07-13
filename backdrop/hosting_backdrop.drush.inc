<?php
/**
 * @file
 * Drush hooks for the hosting_backdrop module.
 *
 * Only the pre-task hook lives here: drush_HOOK_pre_hosting_task is a Drush
 * command pre-hook named by COMMANDFILE, discovered only in *.drush.inc, and
 * it fires for EVERY hosting-task run — hence the task-type self-gate. The
 * post-task hook is a Drupal module_invoke_all and stays in the .module.
 *
 * PHP 5.6 floor applies (parse AND runtime), as in hosting_backdrop.module.
 */

/**
 * Implements drush_HOOK_pre_hosting_task().
 *
 * Populates the backend invocation for the backdrop_upgrade task. The
 * command runs on the SOURCE site context; positional args are the new
 * Backdrop site alias and the target platform context name, mirroring the
 * clone task's arg shape.
 */
function drush_hosting_backdrop_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if ($task->ref->type != 'site' || $task->task_type != 'backdrop_upgrade') {
    return;
  }

  $task->args[2] = '@' . hosting_site_get_domain($task->task_args['new_uri']);
  $platform = node_load($task->task_args['target_platform']);
  $task->args[3] = hosting_context_name($platform->nid);

  // Profile mapping: same short_name when the target carries it; otherwise
  // pass nothing and the backend defaults to 'standard' (and validates it
  // exists on the target). The old_short_name fallback used by stock migrate
  // is deliberately not consulted: relating D7 names to Backdrop profile
  // packages would surface Backdrop platforms as stock-migrate targets.
  $profile = node_load($task->ref->profile);
  if (is_object($profile) && !empty($profile->short_name)) {
    $instance = hosting_package_instance_load(array(
      'i.rid' => $platform->nid,
      'p.short_name' => $profile->short_name,
    ));
    if (is_object($instance) && isset($instance->short_name)) {
      $task->options['profile'] = $instance->short_name;
    }
    else {
      drush_log(dt('Install profile @profile is not present on the target platform; the backend default (standard) applies.', array('@profile' => $profile->short_name)), 'warning');
    }
  }

  if (isset($task->task_args['new_db_server']) && $task->task_args['new_db_server'] && $task->ref->db_server != $task->task_args['new_db_server']) {
    $task->options['new_db_server'] = hosting_context_name($task->task_args['new_db_server']);
  }
}
