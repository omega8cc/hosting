<?php
/**
 * @file
 * Implement drush hooks for the hosting migrate module.
 */

/**
 * Implements drush_HOOK_pre_COMMAND().
 */
function drush_hosting_subdirs_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if ($task->ref->type == 'site' && $task->task_type == 'migrate') {
    $site = $task->ref;

    if (!_hosting_subdirs_is_a_subdir_site($site)) return;

    // Clean up the automatic subdir alias and redirection, as they'll be
    // re-generated with the new domain.
    $subdir_alias = array_search($site->redirection, $site->aliases);
    unset($site->aliases[$subdir_alias]);
    unset($site->redirection);
  }
}

/**
 * Implements drush_HOOK_post_COMMAND().
 */
function drush_hosting_subdirs_post_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');

  // A parent site's vhost includes its subdirectory configs only when a
  // verify finds their directory; a task whose own backend writes the site's
  // configs (install, verify, enable, disable, restore) names each parent
  // whose vhost does not include them yet. A site that is its own parent is
  // never queued from its own verify: a vhost that cannot take the include
  // (redirect-only) would requeue it for good.
  $output = drush_get_context('HOSTING_DRUSH_OUTPUT');
  if (!empty($output['context']['subdirs_parents_to_verify']) && is_array($output['context']['subdirs_parents_to_verify'])) {
    foreach ($output['context']['subdirs_parents_to_verify'] as $parent) {
      $nid = db_query("SELECT n.nid FROM {node} n INNER JOIN {hosting_site} s ON s.vid = n.vid WHERE n.type = 'site' AND n.title = :title AND s.status = :status", array(':title' => $parent, ':status' => HOSTING_SITE_ENABLED))->fetchField();
      if ($nid && !($task->task_type == 'verify' && $nid == $task->rid)) {
        hosting_add_task($nid, 'verify');
      }
    }
  }

  if ($task->ref->type == 'site' && $task->task_type == 'migrate') {
    $site = $task->ref;

    if (!_hosting_subdirs_is_a_subdir_site($site)) return;

    // Trigger a new verify to re-sync the front-end aliases into the back-end
    // context, so that the subdir redirection works.
    hosting_add_task($site->nid, 'verify');
  }
}

