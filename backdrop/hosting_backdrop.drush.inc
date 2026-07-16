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
 * Populates the backend invocation for the backdrop_upgrade,
 * backdrop_d6_upgrade and backdrop_cutover tasks. The upgrade commands run
 * on the SOURCE site context; positional args are the new site alias and
 * the target platform context name, mirroring the clone task's arg shape.
 * Cutover runs on the Backdrop copy and is populated by its own helper.
 */
function drush_hosting_backdrop_pre_hosting_task($task) {
  $task = &drush_get_context('HOSTING_TASK');
  if ($task->ref->type != 'site') {
    return;
  }
  if ($task->task_type == 'backdrop_cutover') {
    _hosting_backdrop_pre_cutover_task($task);
    return;
  }
  if ($task->task_type != 'backdrop_upgrade' && $task->task_type != 'backdrop_d6_upgrade') {
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
  // D6 nuance (backdrop_d6_upgrade): the D6 'default' profile is renamed to
  // 'standard' by the D7 core conversion, so 'default' maps to the backend
  // default silently — only a custom D6 profile is looked up on the target.
  $profile = node_load($task->ref->profile);
  if (is_object($profile) && !empty($profile->short_name)
    && !($task->task_type == 'backdrop_d6_upgrade' && $profile->short_name == 'default')) {
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

/**
 * Populate the backend invocation for the backdrop_cutover task.
 *
 * The current holder of the target domain is resolved HERE, at execution
 * time — not at form time — so a holder that changed since the form was
 * submitted is what actually gets retired (or refused). The backend
 * re-validates fail-closed; these checks make operator mistakes fail the
 * task before its destructive delete leg is even dispatched. The platform
 * and db-server node ids ride as options so the backend can hand them to
 * the chained migrate task (whose task_args are frontend nids).
 */
function _hosting_backdrop_pre_cutover_task($task) {
  $site = $task->ref;
  $domain = isset($task->task_args['target_domain'])
    ? hosting_site_get_domain($task->task_args['target_domain']) : '';

  if ($domain == '' || $domain == hosting_site_get_domain($site->title)) {
    drush_set_error('HOSTING_BACKDROP_CUTOVER_NOOP',
      dt('This site already answers at @domain; nothing to cut over.', array('@domain' => $domain)));
    return;
  }

  $holder = hosting_get_site_by_url($domain);
  if (is_object($holder)) {
    if ($holder->nid == $site->nid) {
      drush_set_error('HOSTING_BACKDROP_CUTOVER_SELF',
        dt('@domain already belongs to this site (as an alias). To make it the main domain use Migrate: an automatic www/non-www companion renames in one step; any other alias must first be removed from the site.', array('@domain' => $domain)));
      return;
    }
    if ($holder->nid == hosting_get_hostmaster_site_nid()) {
      drush_set_error('HOSTING_BACKDROP_CUTOVER_HOSTMASTER',
        dt('@domain is the hosting front-end; it cannot be taken over.', array('@domain' => $domain)));
      return;
    }
    if (hosting_site_get_domain($holder->title) != $domain) {
      drush_set_error('HOSTING_BACKDROP_CUTOVER_ALIAS',
        dt('@domain is an alias of @site, not its main domain. Remove the alias there, or cut over to the main domain instead.',
        array('@domain' => $domain, '@site' => hosting_site_get_domain($holder->title))));
      return;
    }
    if (!hosting_task_dangerous_task_is_allowed('delete', $holder->nid)) {
      drush_set_error('HOSTING_BACKDROP_CUTOVER_GUARDED',
        dt('@site is guarded against deletion; cutover would need to delete it.',
        array('@site' => hosting_site_get_domain($holder->title))));
      return;
    }
    $task->options['cutover_source'] = hosting_context_name($holder->nid);
  }
  else {
    if (!hosting_domain_allowed($domain)) {
      drush_set_error('HOSTING_BACKDROP_CUTOVER_TAKEN',
        dt('No site holds @domain but the name is not allowed (held by another record?).', array('@domain' => $domain)));
      return;
    }
    drush_log(dt('No site currently holds @domain — cutover degrades to a plain rename.', array('@domain' => $domain)), 'info');
  }

  $task->args[2] = $domain;
  $task->options['cutover_platform_nid'] = $site->platform;
  $task->options['cutover_db_server_nid'] = $site->db_server;
}
