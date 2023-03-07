<?php

use Symfony\Component\Process\Process;

/**
 * @file
 * Drush include for the Hosting module's hosting task command.
 */

/**
 * Log a message to the current task's node if possible, the screen otherwise.
 */
function _hosting_task_log($entry) {
  $task = drush_get_context('HOSTING_TASK');
  if ($task->vid) {
    hosting_task_log($task->vid, $entry['type'], $entry['message'], $entry['error'], $entry['timestamp']);
  }
  else {
    _hosting_task_log_print($entry);
  }
  if (drush_get_option('debug', FALSE)) {
    _hosting_task_log_print($entry);
  }
}

/**
 * Switch between Drush 8's OO logging and the older private function.
 */
function _hosting_task_log_print($entry) {
  if (function_exists('_drush_print_log')) {
    // Remove after dropping Drush 6 comaptibility.
    return _drush_print_log($entry);
  }
  else {
    $logger = new Drush\Log\Logger();
    return $logger->log($entry['type'], $entry['message'], $entry);
  }
}

/**
 * Validate hook for the hosting-task Drush command.
 *
 * We do some magic in this command to allow the user to run either a specifc
 * task by specifying a node id or chosen task type by specifying the type of
 * task, e.g. 'verify' or 'migrate'.
 *
 * @see drush_hosting_task()
 */
function drush_hosting_task_validate($id = NULL, $type = NULL) {
  if (empty($id)) {
    return drush_set_error(DRUSH_APPLICATION_ERROR, dt('Missing argument: Task NID or Type. Use --help option for more information.'));
  }

  drush_set_option('user', 1);
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_LOGIN);
  if (is_numeric($id)) {
    $task = node_load($id);
    drush_log(dt("Task node found: !url", array(
      '!url' => url("node/{$task->nid}", array('absolute' => true)),
    )), 'notice');
  }
  elseif (is_string($id) && isset($type)) {
    $ref = hosting_context_load($id);

    if (empty($ref)) {
      return drush_set_error(DRUSH_APPLICATION_ERROR, dt('No context found with name: %name', [
        '%name' => $id,
      ]));
    }
    if ($ref->nid) {

      // Get additional arguments to the drush command and convert them to "task_args" as expected by the task node.
      $task_args = array();

      // Parse task_arguments passed to drush in the format "name=value"
      $arguments = func_get_args();
      $drush_args = array_splice($arguments, 2);
      foreach ($drush_args as $i => $arg) {
        list($name, $value) = explode('=', $arg);
        $task_args[$name] = $value;
      }
      $task = hosting_add_task($ref->nid, $type, $task_args);
      drush_log(dt("Task node created: !url", array(
        '!url' => url("node/{$task->nid}", array('absolute' => true)),
      )), 'ok');
    }
  }

  if ($task->type == 'task') {
    $task->ref = node_load($task->rid);
    $task->options = array(
      'runner' => 'hosting_task',
    );
    $task->context_options = array(
      'context_type' => $task->ref->type,
      'root' => NULL,
      'uri' => NULL,
    );
    $task->args = array();
    $task->changed = REQUEST_TIME;
    $task->executed = REQUEST_TIME;
    /* if not already running, remove the task from the queue
     * this is to avoid concurrent task runs */
    if ($task->task_status == HOSTING_TASK_PROCESSING && !drush_get_option('force', FALSE)) {
      return drush_set_error('HOSTING_TASK_RUNNING', dt("This task is already running, use --force"));
    }
    if ($task->task_status != HOSTING_TASK_QUEUED && !drush_get_option('force', FALSE)) {
      return drush_set_error('HOSTING_TASK_NOT_QUEUED', dt("This task is not queued, use --force"));
    }
    $task->task_status = HOSTING_TASK_PROCESSING;
    $task->revision = TRUE;

    node_save($task);
    drush_set_context('HOSTING_TASK', $task);
    drush_set_context('DRUSH_LOG_CALLBACK', '_hosting_task_log');

    // Load Task Info.
    $tasks_info = hosting_available_tasks($task->ref->type);

    // Find task type and pass through if it needs provision_save.
    if (isset($tasks_info[$task->task_type])) {
      $task->task_info = $tasks_info[$task->task_type];
    }
  }
  else {
    drush_set_error('HOSTING_INVALID_TASK', t("Could not find or create a '!type' task for hosting context '!context'.", array(
      '!type' => $type,
      '!context' => $id,
    )));
  }

}

/**
 * Drush hosting task command.
 *
 * This is the main way that the frontend communicates with the backend. Tasks
 * correspond to backend drush commands, and the results and log of the command
 * are attached to the task for reference.
 *
 * @see drush_hosting_task_validate()
 * @see hook_hosting_TASK_OBJECT_context_options()
 */
function drush_hosting_task()
{

  $task = &drush_get_context('HOSTING_TASK');
  $output = array();
  $mode = drush_get_option('debug', FALSE) ? 'GET' : 'POST';

  chdir(d('@hostmaster')->root);

  // Make sure argument order is correct.
  ksort($task->args);

  // If this task type needs it, run provision-save to create the named context.
  if (!empty($task->task_info['provision_save'])) {
    // Invoke hook_hosting_TASK_OBJECT_context_options()
    // We copy module_invoke_all() here because it doesn't pass by
    // reference and it breaks under PHP 5.3
    $hook = 'hosting_' . $task->ref->type . '_context_options';
    foreach (module_implements($hook) as $module) {
      $function = $module . '_' . $hook;
      call_user_func_array($function, array(&$task));
    }

    // Launch a provision-save for the platform before running it for the site.
    if ($task->ref->type == 'site') {

      // Create a verify task to use as a mock so we can load provision-save options and trigger post_hosting_verify hooks.
      $platform_task = _drush_hosting_task_create_platform_task($task);
      $platform_node = $platform_task->ref;

      // drush_hosting_task_provision_save($platform_node->hosting_name, $platform_task->context_options, t('Saving codebase metadata...'));
      // Save Platform Alias file.
      drush_invoke_process('@none', 'provision-save', array('@' . $platform_node->hosting_name), $platform_task->context_options, array('method' => $mode, 'integrate' => TRUE));

      // Run platform verify task if this is a site install. Provision MUST run platform verify before provision-install.
      // @TODO: Modernize installation of sites.
      if ($task->task_type == 'install' && empty($task->ref->platform_verified)) {
        $output = provision_backend_invoke("@{$platform_node->hosting_name}", "provision-verify", $task->args, $task->options);
        hosting_platform_post_hosting_verify_task($platform_task, $output);
      }
    }

    // Save Current Context Alias file.
    drush_invoke_process('@none', 'provision-save', array('@' . $task->ref->hosting_name), $task->context_options, array('method' => $mode, 'integrate' => TRUE));
  }

  if (($task->ref->type == 'site' && $task->ref->site_status == HOSTING_SITE_DELETED)
    || ($task->ref->type == 'platform' && $task->ref->platform_status == HOSTING_PLATFORM_DELETED)) {

    // We're performing a task on a site that has been deleted...
    // d() will not be returning a site object.
    $alias = '@none';
  } else {
    $alias = $task->ref->hosting_name;
  }

  // Run the actual command.
  $site_record = drush_sitealias_get_record('@' . $alias);
  $task->options['strict'] = 0;

  // Set the drush command to use based on the task_type.
  if (!isset($task->task_command)) {
    $task->task_command = 'provision-' . $task->task_type;
  }

  // Call the drush command.
  $output = provision_backend_invoke($alias, $task->task_command, $task->args, $task->options, $mode);

  // Pass the drush output back to the HOOK_post_hosting_TASK_task() and
  // HOOK_hosting_TASK_task_rollback() hooks, as the $data argument.
  drush_set_context('HOSTING_DRUSH_OUTPUT', $output);

  // On successful delete, remove the named context.
  if ($task->task_type == 'delete' && !drush_get_error()) {

    // Use the task_arg if there is one, hosting_name as a backup. $node->ref->hosting_name may be missing.
    $context_to_delete = $task->task_args['hosting_context'];

    // Run the "provision-save --delete" command on $context_to_delete.
    if ($context_to_delete) {
      drush_log(dt("Deleting back-end context file for !context.", array(
        '!context' => $context_to_delete,
      )), 'notice');
    }
  }
}

/**
 * Helper to just run provision-save
 */
function drush_hosting_task_provision_save($hosting_name, $context_options, $message = NULL) {
  $mode = drush_get_option('debug', FALSE) ? 'GET' : 'POST';
  drush_invoke_process('@none', 'provision-save', array('@' . $hosting_name), $context_options, array('method' => $mode, 'integrate' => TRUE));
}

/**
 * @param $site_task'
 */
function _drush_hosting_task_create_platform_task($site_task) {
  $platform_node = node_load($site_task->ref->platform);
  $platform_task = clone($site_task);
  $platform_task->nid = null;
  $platform_task->vid = null;
  $platform_task->title = "Verify: " . $platform_node->hosting_name;
  $platform_task->task_type = 'verify';
  $platform_task->ref = $platform_node;
  $platform_task->rid = $platform_node->nid;
  $platform_task->context_options = array();

  // Save platform
  $hook = 'hosting_platform_context_options';
  foreach (module_implements($hook) as $module) {
    $function = $module . '_' . $hook;
    call_user_func_array($function, array(&$platform_task));
  }
  $platform_task->context_options['context_type'] = 'platform';
  return $platform_task;
}


/**
 * Rollback hook for the hosting-task Drush command.
 *
 * @see hook_hosting_TASK_TYPE_task_rollback()
 */
function drush_hosting_task_rollback() {
  $task = &drush_get_context('HOSTING_TASK');
  $hook = sprintf("hosting_%s_task_rollback", str_replace('-', '_', $task->task_type));
  drush_log(dt('Invoking :hook hooks.', array(':hook' => $hook)));
  module_invoke_all($hook, $task, drush_get_context('HOSTING_DRUSH_OUTPUT'));
}

/**
 * Post completion hook for the hosting-task Drush command.
 *
 * @see hook_post_hosting_TASK_TYPE_task()
 *
 * Used primarily to trigger hosting_context_delete() for site, platform and
 * server nodes, but only AFTER the task has completed successfully.
 */
function drush_hosting_post_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');

  $hook = sprintf("post_hosting_%s_task", str_replace('-', '_', $task->task_type));
  drush_log(dt('Invoking :hook hooks.', array(':hook' => $hook)));
  module_invoke_all($hook, $task, drush_get_context('HOSTING_DRUSH_OUTPUT'));
}

