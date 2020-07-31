<?php

/**
 * @file
 * Drush include for the Hosting module's hosting task command.
 */

/**
 * Run a command, sending output to drush logs in real time.
 *
 * The Symfony\Component\Process\Process Object is used to run this command.
 * After implementing provision_process(), you can get the Process result object
 * via drush context:
 *
 *   $process = drush_get_context('provision_process_result');
 *   print $process->getExitCode();
 *
 * @param string|array $command_input
 *   The command to run
 * @param null $cwd
 *   The directory to run the command in.
 * @param string $label
 *   A string to display above the command block in the front-end.
 * @param array $env
 *   A list of environment variables to set for the process.
 * @param bool $log_output
 *   Whether or not to send output to drush_log in real time.
 * @param null $error_message
 *   The error message to show after a failure. Defaults to NULL because the UI turning red and the error output is usually enough.
 * @param bool $throw_drush_error
 *   Whether or not to throw a drush error if the process fails. Defaults to TRUE.
 *
 * @return string|void
 *   The output or error output of the command.
 */
function hosting_process($command, $cwd = null, $label = 'Process', $env = array(), $log_output = TRUE, $error_message = NULL, $throw_drush_error = TRUE, $log_type = 'p_info') {
  if (empty($command)) {
    return;
  }

  // @TODO: new Process() below isn't accepting an array.
  if (is_array($command)) {
    $command = implode(' ', $command);
  }

  // Merge in env vars, inheriting the CLI's
  if (is_array($env)) {
    $env = array_merge($_SERVER, $env);
  }
  else {
    $env = $_SERVER;
  }

  // Make sure colors always come through
  $env['TERM'] = 'xterm';

  $process = new \Symfony\Component\Process\Process(escapeshellcmd($command), $cwd, $env);
  $process->setTimeout(NULL);
  if ($log_output) {
    drush_log("[$label] $command", 'p_command');

    $exit_code = $process->run(function ($type, $buffer) use ($log_type) {
      drush_log($buffer, $log_type);
    });
  }
  else {
    $exit_code = $process->run();
  }

  // Save the Provision Process object to Drush Context so that implementors can access the full object.
  drush_set_context('provision_process_result', $process);
  drush_log("Provision process command exited with $exit_code", 'debug');

  // check exit code
  if ($exit_code === 0) {

    if ($log_output) {
      drush_log('', 'p_ok');
    }
    return $process->getOutput();
  }
  else {

    if ($log_output) {
      drush_log('', 'p_error');
    }

    if ($throw_drush_error) {
      drush_set_error('PROVISION_PROCESS_ERROR', !empty($error_message)? $error_message: $process->getErrorOutput());
    }
    return $process->getErrorOutput();
  }
}

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
function drush_hosting_task_validate($id, $type = NULL) {
  drush_set_option('user', 1);
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_LOGIN);
  if (is_numeric($id)) {
    $task = node_load($id);
  }
  elseif (is_string($id) && isset($type)) {
    $ref = hosting_context_load($id);
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
    }
  }

  if ($task->type == 'task') {
    $task->ref = node_load($task->rid);
    $task->options = array();
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
    drush_log(dt("Task starts processing") . ': ' . $task->title, 'queue');

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
function drush_hosting_task() {
  $task = &drush_get_context('HOSTING_TASK');
  $output = array();
  $mode = drush_get_option('debug', FALSE) ? 'GET' : 'POST';

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

    // Save context to meta using a command.
    // @TODO: Continue provision 4.x?
    /** Commenting this out until a new branch is created or something else happens.
    // Call provision context:save command.
    $command = array(
      variable_get('provision_bin_command', '/usr/share/devshop/provision/bin/provision'),
      "-n",
      "--ansi",
      "context:save",
      "--context={$task->ref->hosting_name}"
    );
    **/

    // Build command as an array for compatibility.
    $command = array(
      "drush",
      "provision-save",
      "@{$task->ref->hosting_name}",
    );

    // Prepare "context_options" by converting them to command line options.
    foreach ($task->context_options as $name => $value) {
      $command[] = "--{$name}={$value}";
    }

    // Run the process using "drush_log" to present output.
    // "drush_log then saves each line to the front-end database.
    hosting_process($command, null, t('Saving metadata...'));

    /** @TODO: uncomment when pro4 cli is ready.
    // Save services to the context
    // Servers have services, platforms and sites have service subscriptions.
    if (is_array($task->ref->services)) {
      foreach ($task->ref->services as $service_type => $service_class) {
        $command = array(
          variable_get('provision_bin_command', '/usr/share/devshop/provision/bin/provision'),
          "@{$task->ref->hosting_name}",
          "-n",
          "--ansi",
          'service',
          'add',
          $service_type,
          "--service_type={$service_class->type}"
        );

        foreach ($service_class->provisionProperties() as $name => $value) {
          $value = escapeshellarg($value);
          $command[] = "--{$name}={$value}";
        }
        hosting_process($command, null, t('Saving service metadata...'));
      }
    }
    elseif (is_array($task->ref->servers)) {
      foreach ($task->ref->servers as $service_type => $server_node) {
        $command = array(
          variable_get('provision_bin_command', '/usr/share/devshop/provision/bin/provision'),
          "@{$task->ref->hosting_name}",
          "-n",
          "--ansi",
          "service",
          "add",
          $service_type,
          $server_node->hosting_name,
        );

        // @TODO: Provision4 support for properties.
//        foreach ($subscription->properties as $name => $value) {
//          $value = escapeshellarg($value);
//          $command[] = "--{$name}={$value}";
//        }
        hosting_process($command, null, t('Assigning servers...'));
      }
    }
   */
  }

  if (($task->ref->type == 'site' && $task->ref->site_status == HOSTING_SITE_DELETED)
    || ($task->ref->type == 'platform' && $task->ref->platform_status == HOSTING_PLATFORM_DELETED)) {

    // We're performing a task on a site that has been deleted...
    // d() will not be returning a site object.
    $alias = '@none';
  }
  else {
    $alias = $task->ref->hosting_name;
  }

  if (!isset($task->task_command)) {
    $task->task_command = 'provision-' . $task->task_type;
  }

  // Run the actual command. Adding alias here to work around Drush API.
// @TODO: Convert from Legacy
//  $output = provision_backend_invoke($alias, $task->task_command, $task->args, $task->options, $mode);

  /** @TODO: Put back when/if pro4 gets going.
  // Build the task command.
  // @TODO: Create a function for this?
  $command = array(
    variable_get('provision_bin_command', '/usr/share/devshop/provision/bin/provision'),
    "@{$task->ref->hosting_name}",
    "-n",
    "--ansi",
    $task->task_command
  );
   */

  // Build command as an array for compatibility.
  $command = array(
    "drush",
    $task->task_command,
    "@{$task->ref->hosting_name}",
  );

  // Append each argument.
  foreach ($task->args as $argument) {
    $command[] = $argument;
  }

  // Append each option.
  foreach ($task->options as $name => $value) {
    $value = escapeshellarg($value);
    $command[] = "--{$name}={$value}";
  }
  hosting_process($command, null, t('Running Task...'));

  // Pass the drush output back to the HOOK_post_hosting_TASK_task() and
  // HOOK_hosting_TASK_task_rollback() hooks, as the $data argument.
  drush_set_context('HOSTING_DRUSH_OUTPUT', $output);

  // On succesful delete, remove the named context.
  if ($task->task_type === 'delete' && !drush_get_error()) {
    // Build command as an array for compatibility.
    $command = array(
      "drush",
      "provision-save",
      "@{$task->ref->hosting_name}",
      "--delete"
    );
    hosting_process($command, null, t('Deleting metadata...'));
  }

  // New revision is created at the beginning of function.
  $task->revision = FALSE;
  $task->delta = microtime(TRUE) - $task->executed;
  node_save($task);
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
 */
function drush_hosting_post_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');

  $hook = sprintf("post_hosting_%s_task", str_replace('-', '_', $task->task_type));
  drush_log(dt('Invoking :hook hooks.', array(':hook' => $hook)));
  module_invoke_all($hook, $task, drush_get_context('HOSTING_DRUSH_OUTPUT'));
}
