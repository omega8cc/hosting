<?php

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
    hosting_task_log($task->vid, $entry['type'], $entry['message'], isset($entry['error']) ? $entry['error'] : '', $entry['timestamp']);
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
 * We do some magic in this command to allow the user to run either a specific
 * task by specifying a node id or chosen task type by specifying the type of
 * task, e.g. 'verify' or 'migrate'.
 *
 * @see drush_hosting_task()
 */
function drush_hosting_task_validate($id, $type = NULL) {
  drush_set_option('user', 1);
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_LOGIN);

  // Initialize the task object.
  $task = NULL;

  // Load the task by ID if it's numeric.
  if (is_numeric($id)) {
    $task = node_load($id);
    $is_numeric = $id;
  }
  // Create a new task based on context ID and task type.
  elseif (is_string($id) && isset($type)) {
    $is_string = $id . ' : ' . $type;
    $ref = hosting_context_load($id);
    if ($ref->nid) {
      $ref_nid = $ref->nid;
      // Get additional arguments to the drush command and convert them to "task_args" as expected by the task node.
      $task_args = array();
      // Parse task_arguments passed to drush in the format "name=value".
      // phpcs:ignore PHPCompatibility.FunctionUse.ArgumentFunctionsReportCurrentValue.NeedsInspection
      $arguments = func_get_args();
      $drush_args = array_splice($arguments, 2);
      foreach ($drush_args as $i => $arg) {
        list($name, $value) = explode('=', $arg);
        $task_args[$name] = $value;
      }
      $task = hosting_add_task($ref->nid, $type, $task_args);
    }
  }

  // Ensure the task object is loaded before proceeding.
  if ($task) {
    // Further validation and processing of the task.
    if ($task->type == 'task') {
      $task->ref = node_load($task->rid);
      $task->options = array();
      $task->context_options = array(
        'context_type' => $task->ref->type,
        'master_url' => url('', array('absolute' => TRUE)),
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

      // Now you can safely access task arguments.
      // A backup task created without an explicit mode (CLI hosting-task,
      // API hosting_add_task, queued nightly backups) must produce the same
      // self-contained archive the panel's default produces. Without this
      // default the backend falls to the legacy modeless path, which on
      // mydumper-enabled systems dumps only to the transient tmp_expim store
      // and packs files/private as bare symlinks - an archive that cannot
      // restore its own database. Internal safety copies (clone, migrate,
      // restore) are not created through this runner; their fast path is
      // untouched.
      if ($task->task_type == 'backup' && empty($task->task_args['backup_mode'])) {
        $task->task_args['backup_mode'] = 'site_files_with_mysqldump';
        drush_log(dt("No backup_mode in the task arguments - defaulting to the restorable mode: site_files_with_mysqldump"), 'queue');
      }
      if (!empty($task->task_args['backup_mode'])) {
        $selected_backup_mode = $task->task_args['backup_mode'];
        $bytes_written = file_put_contents(AEGIR_BACKUP_MODE_CTRL, $selected_backup_mode, LOCK_EX);
        if ($bytes_written === FALSE && is_file(AEGIR_BACKUP_MODE_CTRL)) {
          // A leftover file with bad perms can defeat the rewrite; replace it.
          @unlink(AEGIR_BACKUP_MODE_CTRL);
          $bytes_written = file_put_contents(AEGIR_BACKUP_MODE_CTRL, $selected_backup_mode, LOCK_EX);
        }
        if ($bytes_written === FALSE) {
          // The ctrl file is the only transport of the selected mode to the
          // backend; continuing would run the backup with a stale or foreign
          // mode instead of the one selected in the form. Fail closed - but
          // mark the task failed FIRST: erroring out of validate while the
          // node is still QUEUED would make the queue re-select it forever.
          $task->task_status = HOSTING_TASK_ERROR;
          $task->revision = TRUE;
          node_save($task);
          return drush_set_error('HOSTING_TASK_BACKUP_MODE', dt("Could not write the backup mode control file @path - aborting instead of running the backup with a stale mode.", array('@path' => AEGIR_BACKUP_MODE_CTRL)));
        }
      }
      elseif (is_file(AEGIR_BACKUP_MODE_CTRL)) {
        // No mode selected for this task: a leftover mode from an earlier
        // backup task must never leak into this task's implicit backups
        // (restore/delete safety copies and queued nightly backups would
        // silently inherit it - e.g. a stale DB-only mode strips all site
        // files from them). Only a genuinely stale file is removed: a young
        // one may be a concurrent parallel worker's live mode, written
        // seconds ago and not yet consumed by its backend.
        $ctrl_mtime = filemtime(AEGIR_BACKUP_MODE_CTRL);
        if ($ctrl_mtime !== FALSE && (REQUEST_TIME - $ctrl_mtime) > 900) {
          @unlink(AEGIR_BACKUP_MODE_CTRL);
          if (is_file(AEGIR_BACKUP_MODE_CTRL)) {
            drush_log(dt("Could not remove the stale backup mode control file @path - implicit backups may inherit its mode.", array('@path' => AEGIR_BACKUP_MODE_CTRL)), 'warning');
          }
        }
      }

      $task->task_status = HOSTING_TASK_PROCESSING;
      $task->revision = TRUE;

      // Save the task node after setting its status.
      node_save($task);

      // Record this runner's PID on the fresh PROCESSING revision. The final
      // status is written only in this process's shutdown handler, so a
      // runner that dies without it leaves the row at PROCESSING forever;
      // the dispatcher's reaper needs the PID to tell such an orphan from a
      // genuinely running task. Guarded: the column arrives with update
      // 7303, and this code can run against a not-yet-updated schema.
      if (db_field_exists('hosting_task', 'pid')) {
        db_update('hosting_task')
          ->fields(array('pid' => (int) getmypid()))
          ->condition('vid', $task->vid)
          ->execute();
      }

      // Set the Drush contexts.
      drush_set_context('HOSTING_TASK', $task);
      drush_set_context('DRUSH_LOG_CALLBACK', '_hosting_task_log');
      drush_log(dt("Task starts processing") . ': ' . $task->title, 'queue');

      if (!empty($task->task_args['backup_mode'])) {

        // Extract the backup_mode from the task arguments if available.
        if (isset($id)) {
          drush_log(dt("Extracting the backup_mode from the task arguments if available") . ': ' . $id, 'queue');
        }
        if (isset($is_numeric)) {
          drush_log(dt("Task is_numeric node to load was ") . ': ' . $is_numeric, 'queue');
        }
        if (isset($is_string)) {
          drush_log(dt("Task is_string and type was ") . ': ' . $is_string, 'queue');
        }
        if (isset($ref_nid)) {
          drush_log(dt("Task ref-nid was ") . ': ' . $ref_nid, 'queue');
        }
        if (isset($type) && isset($name) && isset($task_args[$name])) {
          drush_log(dt("Task add values were ") . ':: ' . $ref->nid . ' :: ' . $type . ' :: ' . $task_args[$name], 'queue');
        }
        if (isset($selected_backup_mode)) {
          drush_log(dt("Uses drush_set_option to set selected_backup_mode to") . ': ' . $selected_backup_mode, 'queue');
        }
        if (defined('AEGIR_BACKUP_MODE_CTRL')) {
          drush_log(dt("AEGIR_BACKUP_MODE_CTRL was ") . ': ' . AEGIR_BACKUP_MODE_CTRL, 'queue');
        }

        drush_set_option('selected_backup_mode', $selected_backup_mode);

        // Verify if the operation was successful.
        if ($bytes_written === FALSE) {
          drush_log(dt("Failed to write the backup mode to the control file"), 'queue');
        }
        else {
          drush_log(dt("Backup mode successfully written to the control file"), 'queue');
        }
      }
      else {
        // Only non-backup task types reach this branch now; a backup task
        // always carries a mode (explicit or the default applied above).
        drush_log(dt("No backup mode involved for this task type"), 'debug');
      }

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
  else {
    drush_set_error('HOSTING_TASK_LOAD_FAILED', t("Failed to load or create a task for ID '!id'.", array('!id' => $id)));
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

  // The backup mode ctrl file is a one-shot channel consumed by the backend
  // invoke above; removing it here keeps a later task's implicit backups from
  // inheriting this task's mode.
  if (!empty($task->task_args['backup_mode']) && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    @unlink(AEGIR_BACKUP_MODE_CTRL);
    if (is_file(AEGIR_BACKUP_MODE_CTRL)) {
      drush_log(dt("Could not remove the backup mode control file @path after the task - implicit backups may inherit its mode.", array('@path' => AEGIR_BACKUP_MODE_CTRL)), 'warning');
    }
  }

  // On succesful delete, remove the named context.
  if ($task->task_type === 'delete' && !drush_get_error()) {
    drush_invoke_process('@none', 'provision-save', array('@' . $task->ref->hosting_name), array('delete' => TRUE), array('method' => $mode, 'integrate' => TRUE));
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
