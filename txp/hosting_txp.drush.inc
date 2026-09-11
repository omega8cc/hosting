<?php
/**
 * @file
 * Drush hooks for the hosting_txp module.
 *
 * Only the pre-task hook lives here: drush_HOOK_pre_hosting_task is a Drush
 * command pre-hook named by COMMANDFILE, discovered only in *.drush.inc, and
 * it fires for EVERY hosting-task run — hence the task-type self-gate. The
 * post-task hooks are Drupal module_invoke_all and stay in the .module.
 *
 * Plain D7 idiom throughout because Hostmaster IS Drupal 7, and this file is
 * loaded under the Drush-8 backend on every box that carries the hosting tree
 * (the 5.6 floor, independent of Textpattern's own 8.4 floor).
 */

/**
 * Implements drush_HOOK_pre_hosting_task().
 *
 * Populates the backend invocation for the txp_upgrade, txp_rename and
 * txp_cutover tasks. The upgrade command runs on the SOURCE site context;
 * positional args are the new site alias and the target platform context
 * name, mirroring the clone task's arg shape. Cutover runs on the upgraded
 * copy and is populated by its own helper.
 */
function drush_hosting_txp_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if (!isset($task->ref) || !isset($task->ref->type) || $task->ref->type != 'site') {
    return;
  }
  switch ($task->task_type) {
    case 'txp_upgrade':
      _hosting_txp_pre_upgrade_task($task);
      return;

    case 'txp_rename':
      _hosting_txp_pre_rename_task($task);
      return;

    case 'txp_cutover':
      _hosting_txp_pre_cutover_task($task);
      return;
  }
}

/**
 * Populate the backend invocation for the txp_upgrade task.
 *
 * No profile option: a Textpattern platform carries the one synthetic
 * 'textpattern' profile and the backend resolves it itself. The db server
 * rides as an option only when it actually changes, exactly as clone does.
 */
function _hosting_txp_pre_upgrade_task($task) {
  $new_uri = isset($task->task_args['new_uri']) ? $task->task_args['new_uri'] : '';
  if ($new_uri === '') {
    drush_set_error('HOSTING_TXP_UPGRADE_NO_URI',
      dt('No new domain name was given for the upgraded copy.'));
    return;
  }
  $platform = NULL;
  if (!empty($task->task_args['target_platform'])) {
    $platform = node_load($task->task_args['target_platform']);
  }
  if (!is_object($platform)) {
    drush_set_error('HOSTING_TXP_UPGRADE_NO_TARGET',
      dt('The target platform for the upgraded copy could not be loaded.'));
    return;
  }

  $task->args[2] = '@' . hosting_site_get_domain($new_uri);
  $task->args[3] = hosting_context_name($platform->nid);

  if (isset($task->task_args['new_db_server']) && $task->task_args['new_db_server']
    && $task->ref->db_server != $task->task_args['new_db_server']) {
    $task->options['new_db_server'] = hosting_context_name($task->task_args['new_db_server']);
  }
}

/**
 * Populate the backend invocation for the txp_rename task.
 *
 * One positional argument, the new site alias. The cutover chain queues this
 * task through the string form of hosting-task ("new_uri=<domain>"), so the
 * argument is built from task_args here for both entry paths.
 */
function _hosting_txp_pre_rename_task($task) {
  $new_uri = isset($task->task_args['new_uri']) ? $task->task_args['new_uri'] : '';
  if ($new_uri === '') {
    drush_set_error('HOSTING_TXP_RENAME_NO_URI',
      dt('No new domain name was given for the rename.'));
    return;
  }
  $domain = hosting_site_get_domain($new_uri);
  if ($domain === hosting_site_get_domain($task->ref->title)) {
    drush_set_error('HOSTING_TXP_RENAME_NOOP',
      dt('This site already answers at @domain; nothing to rename.', array('@domain' => $domain)));
    return;
  }
  $task->args[2] = '@' . $domain;
}

/**
 * Populate the backend invocation for the txp_cutover task.
 *
 * The current holder of the target domain is resolved HERE, at execution
 * time — not at form time — so a holder that changed since the form was
 * submitted is what actually gets retired (or refused). The backend
 * re-validates fail-closed; these checks make operator mistakes fail the task
 * before its destructive delete leg is even dispatched. The platform and
 * db-server node ids ride as options so the backend can hand them to the
 * chained rename task (whose task_args are frontend nids).
 */
function _hosting_txp_pre_cutover_task($task) {
  $site = $task->ref;
  $domain = isset($task->task_args['target_domain'])
    ? hosting_site_get_domain($task->task_args['target_domain']) : '';

  if ($domain == '' || $domain == hosting_site_get_domain($site->title)) {
    drush_set_error('HOSTING_TXP_CUTOVER_NOOP',
      dt('This site already answers at @domain; nothing to cut over.', array('@domain' => $domain)));
    return;
  }

  $holder = hosting_get_site_by_url($domain);
  if (is_object($holder)) {
    if ($holder->nid == $site->nid) {
      drush_set_error('HOSTING_TXP_CUTOVER_SELF',
        dt('@domain already belongs to this site (as an alias). Remove that alias from the site, then use Rename to make @domain its main domain.', array('@domain' => $domain)));
      return;
    }
    if ($holder->nid == hosting_get_hostmaster_site_nid()) {
      drush_set_error('HOSTING_TXP_CUTOVER_HOSTMASTER',
        dt('@domain is the hosting front-end; it cannot be taken over.', array('@domain' => $domain)));
      return;
    }
    if (hosting_site_get_domain($holder->title) != $domain) {
      drush_set_error('HOSTING_TXP_CUTOVER_ALIAS',
        dt('@domain is an alias of @site, not its main domain. Remove the alias there, or cut over to the main domain instead.',
        array('@domain' => $domain, '@site' => hosting_site_get_domain($holder->title))));
      return;
    }
    if (!hosting_task_dangerous_task_is_allowed('delete', $holder->nid)) {
      drush_set_error('HOSTING_TXP_CUTOVER_GUARDED',
        dt('@site is guarded against deletion; cutover would need to delete it.',
        array('@site' => hosting_site_get_domain($holder->title))));
      return;
    }
    // The holder is whatever answers to the domain typed into the dialog, so
    // the account that queued the task must be allowed to administer it —
    // the task permission is granted per account, not per site, and the
    // delete leg below is dispatched on the holder, not on $site. Task nodes
    // carry the queueing account in uid; an unloadable one refuses.
    $account = isset($task->uid) ? user_load($task->uid) : FALSE;
    if (!_hosting_txp_may_retire_site($holder, $account)) {
      drush_set_error('HOSTING_TXP_CUTOVER_DENIED',
        dt('You may not retire @site, and cutover would delete it.',
        array('@site' => hosting_site_get_domain($holder->title))));
      return;
    }
    $task->options['cutover_source'] = hosting_context_name($holder->nid);
  }
  else {
    if (!hosting_domain_allowed($domain)) {
      drush_set_error('HOSTING_TXP_CUTOVER_TAKEN',
        dt('No site holds @domain but the name is not allowed (held by another record?).', array('@domain' => $domain)));
      return;
    }
    drush_log(dt('No site currently holds @domain — cutover degrades to a plain rename.', array('@domain' => $domain)), 'info');
  }

  $task->args[2] = $domain;
  $task->options['cutover_platform_nid'] = $site->platform;
  $task->options['cutover_db_server_nid'] = $site->db_server;
}
