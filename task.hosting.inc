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
        $output = provision_backend_invoke("@{$platform_node->hosting_name}", "provision-verify");
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
  }
  else {
    $alias = $task->ref->hosting_name;
  }

  if (!isset($task->task_command)) {
    $task->task_command = 'provision-' . $task->task_type;
  }
  // Run the actual command. Adding alias here to work around Drush API.
  $output = provision_backend_invoke($alias, $task->task_command, $task->args, $task->options, $mode);
  // Pass the drush output back to the HOOK_post_hosting_TASK_task() and
  // HOOK_hosting_TASK_task_rollback() hooks, as the $data argument.
  drush_set_context('HOSTING_DRUSH_OUTPUT', $output);

  // On succesful delete, remove the named context.
  if ($task->task_type === 'delete' && !drush_get_error()) {
    drush_invoke_process('@none', 'provision-save', array('@' . $task->ref->hosting_name), array('delete' => TRUE), array('method' => $mode, 'integrate' => TRUE));
  }

  // New revision is created at the beginning of function.
  $task->revision = FALSE;
  $task->delta = microtime(TRUE) - $task->executed;
  node_save($task);
  // @TODO: END OF PROVISION 3 CODE

  /** @TODO: BEGINNING OF MODERNIZATION HACKS:
  $task = &drush_get_context('HOSTING_TASK');
  $output = array();
  $mode = drush_get_option('debug', FALSE) ? 'GET' : 'POST';

  // Make sure argument order is correct.
  ksort($task->args);

  // If this task type needs it, run provision-save to create the named context.
  if (!empty($task->task_info['provision_save'])) {

    // Launch a provison-save for the platform before running it for the site.
    if ($task->ref->type == 'site') {

      // Create a verify task
      $platform_node = node_load($task->ref->platform);

      $platform_task = clone($task);
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

      drush_hosting_task_provision_save($platform_node->hosting_name, $platform_task->context_options, t('Saving codebase metadata...'));

      // Run platform verify task if this is a site or platform verify.
      if ($task->task_type == 'verify' || $task->task_type == 'install') {
//        hosting_process(array(
//          "drush",
//          "@{$platform_task->ref->hosting_name}",
//          "provision-verify",
//        ), null, t('Preparing codebase...'));
        $output = provision_backend_invoke("@{$platform_task->ref->hosting_name}", "provision-verify", $mode);
      }
    }


    // Prepare context options and run provision-save.
    // Invoke hook_hosting_TASK_OBJECT_context_options()
    // We copy module_invoke_all() here because it doesn't pass by
    // reference and it breaks under PHP 5.3
    $hook = 'hosting_' . $task->ref->type . '_context_options';
    foreach (module_implements($hook) as $module) {
      $function = $module . '_' . $hook;
      call_user_func_array($function, array(&$task));
    }

    drush_hosting_task_provision_save($task->ref->hosting_name, $task->context_options, t('Saving metadata...'));

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

  if (empty($task->task_command)) {
    $task->task_command = 'provision-' . $task->task_type;
  }

  // Run the actual command. Adding alias here to work around Drush API.
// @TODO: Convert from Legacy
  $output = provision_backend_invoke($alias, $task->task_command, $task->args, $task->options, $mode);

  /**
  // Build the task command.
  // @TODO: Create a function for this?
  $command = array(
    variable_get('provision_bin_command', '/usr/share/devshop/provision/bin/provision'),
    "@{$task->ref->hosting_name}",
    "-n",
    "--ansi",
    $task->task_command
  );

  // Build command as an array for compatibility.
  $command = array(
    "drush",
    "@{$task->ref->hosting_name}",
    $task->task_command,
  );

  // Append each argument.
  foreach ($task->args as $argument) {
    if (!empty($argument)) {
      $command[] = escapeshellarg($argument);
    }
  }

  // Append each option.
  foreach ($task->options as $name => $value) {
    if (!empty($value)) {
      // If $value is just TRUEish, don't show value in the command line
      if ((string) $value == "1") {
        $command[] = "--{$name}";
      }
      else {
        $value = escapeshellarg($value);
        $command[] = "--{$name}={$value}";
      }
    }
  }

  // Append --strict option because... hosting tasks often pass arbitrary options.
  $command[] = '--strict=0';
  hosting_process($command, null, t('Running Task...'));

  // Pass the drush output back to the HOOK_post_hosting_TASK_task() and
  // HOOK_hosting_TASK_task_rollback() hooks, as the $data argument.
  drush_set_context('HOSTING_DRUSH_OUTPUT', $output);

  // On successful delete, remove the named context.
  if ($task->task_type == 'delete' && !drush_get_error()) {

    // Use the task_arg if there is one, hosting_name as a backup. $node->ref->hosting_name may be missing.
    $context_to_delete = $task->task_args['hosting_context']?:
      ($task->ref->hosting_name?: FALSE);

    // Run the "provision-save --delete" command on $context_to_delete.
    if ($context_to_delete) {
      drush_log(dt("Deleting back-end context file for !context.", array(
        '!context' => $context_to_delete,
      )), 'notice');

      /* Reverting modernization for now
      // Build command as an array for compatibility.
      $command = array(
        "drush",
        "provision-save",
        "@{$task->ref->hosting_name}",
        "--delete"
      );
      hosting_process($command, null, t('Deleting metadata...'));
      drush_invoke_process('@none', 'provision-save', array('@' . $task->ref->hosting_name), array('delete' => TRUE), array('method' => $mode, 'integrate' => TRUE));
    }
    // If no context name was found, throw a warning.
    else {
      drush_log(dt('No "hosting_name" property was found on the !type node (!url). Unable to delete back-end context file.', array(
        '!type' => $task->ref->type,
        '!url' => url("node/$task->rid", array('absolute' => TRUE)),
      )), 'warning');
    }
  }

  // New revision is created at the beginning of function.
  $task->revision = FALSE;
  $task->delta = microtime(TRUE) - $task->executed;
  node_save($task);
  // @TODO: END OF MODERNIZATION HACKS
   */
}

/**
 * Helper to just run provision-save
 */
function drush_hosting_task_provision_save($hosting_name, $context_options, $message = NULL) {

  /*
  // Build command as an array for compatibility.
  $command = array(
    "drush",
    "provision-save",
    "@{$hosting_name}",
  );

  // Prepare "context_options" by converting them to command line options.
  foreach ($context_options as $name => $value) {
    if (!is_string($value)) {
      continue;
    }

    // Keep empty values here because it's being saved to context.
    // If values don't exist here, the existing drush context properties will remain unchanged.
    $value = escapeshellarg($value);
    $command[] = "--{$name}={$value}";
  }

  // Run the process using "drush_log" to present output.
  // "drush_log then saves each line to the front-end database.
  // @TODO: Leaving legacy provision in place for this PR.
  $message = $message?: t('Saving metadata...');
  hosting_process($command, null, t($message));
  */
  $mode = drush_get_option('debug', FALSE) ? 'GET' : 'POST';
  drush_invoke_process('@none', 'provision-save', array('@' . $hosting_name), $context_options, array('method' => $mode, 'integrate' => TRUE));
  // hosting_process($command, null, t($message));
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
