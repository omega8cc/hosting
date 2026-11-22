<?php

/**
 * @file
 * Drush hooks for the hosting_task module.
 */

/**
 * Implements hook_drush_init().
 *
 * This function registers a shutdown function to update the task status after
 * drush completes it's run.
 *
 * The call to register_shutdown_function() is needed to ensure we get a status
 * update no matter what happens with drush.
 */
function hosting_task_drush_init() {
  // Update a task's status after Drush operations are complete.
  register_shutdown_function('hosting_task_drush_update_status');
}

/**
 * Shutdown function to catch any task status.
 */
function hosting_task_drush_update_status() {
  $task = drush_get_context('HOSTING_TASK');
  if (!empty($task)) {
    hosting_task_drush_log_missing_errors($task);
    $message = _hosting_parse_error_code(hosting_task_update_status($task));
    drush_log(dt('Updated task status to "!log"', array('!log' => $message)), 'info');
  }
}

/**
 * Writes the errors a run ended with into its task's log where it lacks them.
 *
 * A task's status is read from its log. A runner started by another Drush
 * process (a task that runs a task) is in backend mode, where
 * drush_set_error() hands each error to the caller and logs nothing in this
 * process: such a task ended as successful, or with a warning, whatever
 * failed in it, and its log never said why.
 *
 * An error code the log already holds a line for is left alone, so a runner
 * that logged its errors itself gains nothing here.
 */
function hosting_task_drush_log_missing_errors($task) {
  if (!is_object($task) || empty($task->vid)) {
    return;
  }
  $errors = drush_get_context('DRUSH_ERROR_LOG', array());
  if (empty($errors) || !is_array($errors)) {
    return;
  }
  try {
    foreach ($errors as $code => $messages) {
      $code = (string) $code;
      $logged = db_query_range("SELECT 1 FROM {hosting_task_log} WHERE vid = :vid AND type IN ('error', 'failed') AND error = :code", 0, 1, array(
        ':vid' => $task->vid,
        ':code' => $code,
      ))->fetchField();
      if ($logged) {
        continue;
      }
      foreach ((array) $messages as $message) {
        $message = is_string($message) ? trim($message) : '';
        hosting_task_log($task->vid, 'error', $message !== '' ? $message : $code, $code);
      }
    }
  }
  catch (Exception $e) {
    // The status update that follows this call is tried whatever happens here.
  }
}
