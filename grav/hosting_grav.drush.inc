<?php
/**
 * @file
 * Drush hooks for hosting_grav.
 *
 * Only the pre-task hook lives here: drush_HOOK_pre_hosting_task is a Drush
 * command pre-hook named by COMMANDFILE, discovered only in *.drush.inc, and
 * it fires for EVERY hosting-task run — hence the task-type self-gate. The
 * post-task hooks are Drupal module_invoke_all and stay in the .module.
 *
 * PHP 5.6 floor: this file is loaded under the Drush-8 backend on every box
 * that carries the hosting tree.
 */

/**
 * Implements drush_hook_pre_hosting_task().
 *
 * Populates the backend invocation for the Grav task family and normalises
 * the db server on the copy-making tasks. Runs after hosting_clone's own pre
 * hook (commandfiles are invoked in name order) on the shared HOSTING_TASK
 * context, so both the option the backend receives and the argument the
 * post-task reads are normalised.
 */
function drush_hosting_grav_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if (!isset($task->ref) || !isset($task->ref->type) || $task->ref->type != 'site') {
    return;
  }
  switch ($task->task_type) {
    case 'clone':
    case 'migrate':
      if (isset($task->ref->platform) && hosting_grav_platform_is_grav($task->ref->platform)) {
        _hosting_grav_pin_db_server($task);
      }
      return;

    case 'grav_copy_upgrade':
      _hosting_grav_pre_copy_upgrade_task($task);
      return;

    case 'grav_rename':
      _hosting_grav_pre_rename_task($task);
      return;

    case 'grav_cutover':
      _hosting_grav_pre_cutover_task($task);
      return;
  }
}

/**
 * Pin the copy's database server to the source's.
 *
 * A Grav capsule has no database, and the copy a clone (or a migrate, or the
 * copy upgrade) makes keeps the source's "none" database server whatever the
 * task says: the backend pins it and says so, but the FRONT END registers the
 * copy from the task arguments (hosting_clone_post_hosting_clone_task()
 * copies task_args['new_db_server'] onto the new node when it differs). The
 * confirm dialogs already pin the value for a Grav site; this is the belt for
 * a task queued without a dialog (hosting_add_task(), the API, a script), so
 * the copy's node and the copy's backend context never disagree.
 */
function _hosting_grav_pin_db_server($task) {
  if (isset($task->task_args['new_db_server']) && $task->task_args['new_db_server'] != $task->ref->db_server) {
    drush_log(dt('The database server @nid requested for the copy is ignored: a Grav capsule has no database and keeps the source\'s server.',
      array('@nid' => $task->task_args['new_db_server'])), 'ok');
  }
  $task->task_args['new_db_server'] = $task->ref->db_server;
  unset($task->options['new_db_server']);
}

/**
 * Populate the backend invocation for the grav_copy_upgrade task.
 *
 * The command runs on the SOURCE site context; the positional arguments are
 * the new site alias and the target platform context name, exactly the clone
 * task's argument shape. No profile option: a Grav platform carries the one
 * synthetic 'grav' profile and the backend resolves it itself.
 */
function _hosting_grav_pre_copy_upgrade_task($task) {
  $new_uri = isset($task->task_args['new_uri']) ? $task->task_args['new_uri'] : '';
  if ($new_uri === '') {
    drush_set_error('HOSTING_GRAV_COPY_UPGRADE_NO_URI',
      dt('No new domain name was given for the upgraded copy.'));
    return;
  }
  $platform = NULL;
  if (!empty($task->task_args['target_platform'])) {
    $platform = node_load($task->task_args['target_platform']);
  }
  if (!is_object($platform)) {
    drush_set_error('HOSTING_GRAV_COPY_UPGRADE_NO_TARGET',
      dt('The target platform for the upgraded copy could not be loaded.'));
    return;
  }

  $task->args[2] = '@' . hosting_site_get_domain($new_uri);
  $task->args[3] = hosting_context_name($platform->nid);

  _hosting_grav_pin_db_server($task);
}

/**
 * Populate the backend invocation for the grav_rename task.
 *
 * One positional argument, the new site alias. The cutover chain queues this
 * task through the string form of hosting-task ("new_uri=<domain>"), so the
 * argument is built from task_args here for both entry paths.
 */
function _hosting_grav_pre_rename_task($task) {
  $new_uri = isset($task->task_args['new_uri']) ? $task->task_args['new_uri'] : '';
  if ($new_uri === '') {
    drush_set_error('HOSTING_GRAV_RENAME_NO_URI',
      dt('No new domain name was given for the rename.'));
    return;
  }
  $domain = hosting_site_get_domain($new_uri);
  if ($domain === hosting_site_get_domain($task->ref->title)) {
    drush_set_error('HOSTING_GRAV_RENAME_NOOP',
      dt('This site already answers at @domain; nothing to rename.', array('@domain' => $domain)));
    return;
  }
  $task->args[2] = '@' . $domain;
}

/**
 * Populate the backend invocation for the grav_cutover task.
 *
 * The current holder of the target domain is resolved HERE, at execution
 * time — not at form time — so a holder that changed since the form was
 * submitted is what actually gets retired (or refused). The backend
 * re-validates fail-closed; these checks make operator mistakes fail the task
 * before its destructive delete leg is even dispatched. The platform and
 * db-server node ids ride as options so the backend can hand them to the
 * chained rename task (whose task_args are frontend nids).
 */
function _hosting_grav_pre_cutover_task($task) {
  $site = $task->ref;
  $domain = isset($task->task_args['target_domain'])
    ? hosting_site_get_domain($task->task_args['target_domain']) : '';

  if ($domain == '' || $domain == hosting_site_get_domain($site->title)) {
    drush_set_error('HOSTING_GRAV_CUTOVER_NOOP',
      dt('This site already answers at @domain; nothing to cut over.', array('@domain' => $domain)));
    return;
  }

  $holder = hosting_get_site_by_url($domain);
  if (is_object($holder)) {
    if ($holder->nid == $site->nid) {
      drush_set_error('HOSTING_GRAV_CUTOVER_SELF',
        dt('@domain already belongs to this site (as an alias). Remove that alias from the site, then use Rename to make @domain its main domain.', array('@domain' => $domain)));
      return;
    }
    if ($holder->nid == hosting_get_hostmaster_site_nid()) {
      drush_set_error('HOSTING_GRAV_CUTOVER_HOSTMASTER',
        dt('@domain is the hosting front-end; it cannot be taken over.', array('@domain' => $domain)));
      return;
    }
    if (hosting_site_get_domain($holder->title) != $domain) {
      drush_set_error('HOSTING_GRAV_CUTOVER_ALIAS',
        dt('@domain is an alias of @site, not its main domain. Remove the alias there, or cut over to the main domain instead.',
        array('@domain' => $domain, '@site' => hosting_site_get_domain($holder->title))));
      return;
    }
    if (!hosting_task_dangerous_task_is_allowed('delete', $holder->nid)) {
      drush_set_error('HOSTING_GRAV_CUTOVER_GUARDED',
        dt('@site is guarded against deletion; cutover would need to delete it.',
        array('@site' => hosting_site_get_domain($holder->title))));
      return;
    }
    $task->options['cutover_source'] = hosting_context_name($holder->nid);
  }
  else {
    if (!hosting_domain_allowed($domain)) {
      drush_set_error('HOSTING_GRAV_CUTOVER_TAKEN',
        dt('No site holds @domain but the name is not allowed (held by another record?).', array('@domain' => $domain)));
      return;
    }
    drush_log(dt('No site currently holds @domain — cutover degrades to a plain rename.', array('@domain' => $domain)), 'info');
  }

  $task->args[2] = $domain;
  $task->options['cutover_platform_nid'] = $site->platform;
  $task->options['cutover_db_server_nid'] = $site->db_server;
}
