<?php
/**
 * @file
 * Drush include for the hosting_alias module.
 */

function drush_hosting_alias_pre_hosting_task() {
  if (module_exists('hosting_alias')) {
    $task = &drush_get_context('HOSTING_TASK');
    if ($task->ref->type == 'site') {
      $all_aliases = hosting_alias_get_aliases($task->ref);
      $aliases = implode(',', $all_aliases);
      $task->context_options['aliases'] = ($aliases) ? $aliases : 'null';

      $redirection = db_query("SELECT redirection FROM {hosting_site_alias} WHERE vid = :vid", array(':vid' => $task->ref->vid))->fetchField();
      // The backend gets the target the vhost serves, so the login link and
      // the certificate names follow it: a login link made under a target
      // the site was renamed away from could boot another site of the
      // platform holding that name.
      $redirection = hosting_alias_effective_redirection($task->ref->title, $redirection, $all_aliases);
      $task->context_options['redirection'] = ($redirection) ? $redirection : 'null';
    }
  }
}

/**
 * Implements hook_drush_context_import().
 *
 * If we're importing a site, then import the aliases and redirection values.
 */
function hosting_alias_drush_context_import($context, &$node) {
  if ($context->type == 'site') {
    $node->aliases = $context->aliases;
    $node->redirection = $context->redirection;
  }
}
