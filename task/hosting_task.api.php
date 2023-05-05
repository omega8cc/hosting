<?php

/**
 * @file
 * Hooks provided by the hosting tasks module.
 */

/**
 * @addtogroup backend-frontend-IPC
 * @{
 */

/**
 * Define tasks that can be executed in the front-end.
 *
 * @return array
 *   An array of arrays of tasks that can be executed by the front-end.
 *   The keys of the outer array should be the object that tasks operate on, for
 *   example 'site', 'platform' or 'server'. The values of the outer array
 *   should be an array of tasks keyed by task type, the value should be an
 *   array that defines the task. Valid keys for defining tasks are:
 *   - 'title': (required) The human readable name of the task.
 *   - 'command': (optional) The drush command to run. If not included, will assume "provision-TASKTYPE".
 *   - 'description': (optional) The human readable description of the task.
 *   - 'weight': (optional) The weight of the task when displayed in lists.
 *   - 'dialog' (optional) Set to TRUE to indicate that this task requires a
 *      dialog to be shown to the user to confirm the execution of the task.
 *   - 'hidden' (optional) Set to TRUE to hide the task in the front-end UI, the
 *      task will still be available for execution by the front-end however.
 *   - 'access callback' (optional) An access callback to determine if the user
 *      can access the task, defaults to 'hosting_task_menu_access'.
 *   - 'provision_save' (optional, defaults to FALSE) A flag that tells
 *      provision that a "provision-save" command needs to happen before this
 *      task can be run, used for tasks like Verify, Install, and Import.
 *      If you implement this option, you should implement
 *      hook_hosting_TASK_OBJECT_context_options() in order to pass parameters
 *      to the provision-save command.
 *
 * @see hosting_available_tasks()
 * @see hosting_task_TASK_TYPE_form()
 */
function hook_hosting_tasks() {
  // From hosting_clone_hosting_tasks().
  $options = [];

  $options['site']['clone'] = ['title' => t('Clone'), 'description' => t('You may want to read our <a href="https://learn.omega8.cc/the-best-recipes-for-disaster-139" target=_blank><strong>best recipes for disaster</strong></a> before going further. You can make a copy of your site using this form, but <strong>avoid changing its platform here at the same time</strong>. If you wish to securely change the platform this site (or its cloned copy) is hosted on, <i>Clone</i> it in its <strong>current</strong> platform first and then use <i>Migrate</i> to move it to the new platform. Hint: if you wish to change only the main domain of your site, use <i>Migrate</i>, not <i>Clone</i>.'), 'weight' => 5, 'dialog' => TRUE, 'command' => 'provision-clone'];
  return $options;
}

/**
 * Alter front-end tasks defined by other modules.
 *
 * @param $tasks
 *   An array of tasks defined by other modules. Keys of the outer array are the
 *   types of objects that the tasks operate on, e.g. 'site', 'platform' or
 *   'server', values are arrays of the tasks that apply to those objects.
 *
 * @see hook_hosting_tasks
 */
function hook_hosting_tasks_alter(&$tasks) {
  // Change the title of the site's clone task.
  if (isset($tasks['site']['clone'])) {
    $tasks['site']['clone']['title'] = t('Site clone');
  }
}

/**
 * Add fields to the task confirmation form.
 *
 * @param $node
 *   The node on which the task is being called.
 *
 * @see hosting_task_confirm_form()
 * @see hosting_site_list_form()
 */
function hosting_task_TASK_TYPE_form($node) {
  // From hosting_task_clone_form()
  $form = hosting_task_migrate_form($node);
  $form['new_uri']['#description'] = t('The new domain name of the clone site.');
  return $form;
}

/**
 * Validate the form data defined in hosting_task_TASK_TYPE_form().
 *
 * @see hosting_task_confirm_form()
 */
function hosting_task_TASK_TYPE_form_validate($form, &$form_state) {
  // From hosting_task_clone_form_validate()
  $site = $form['parameters']['#node'];

  $url = hosting_site_get_domain($form_state['values']['parameters']['new_uri']);
  if ($url == hosting_site_get_domain($site->title)) {
    form_set_error('new_uri', t("To clone a site you need to specify a new Domain name to clone it to."));
  }
  else {
    hosting_task_migrate_form_validate($form, $form_state);
  }

}

/**
 * Alter the query that selects the next tasks to run.
 *
 * You may use this hook to prioritise one type of task over another, or to
 * prefer one client over another etc.
 *
 * @see hosting_get_new_tasks
 *
 * @param QueryAlterableInterface $query
 *   The structured query that will select the next tasks to run.
 */
function hook_query_hosting_get_new_tasks_alter(QueryAlterableInterface $query) {
  // Change the sort ordering so that newer tasks are preferred to older ones.
  $order_by = &$query->getOrderBy();
  $order_by['n.changed'] = 'DESC';
  $order_by['n.nid'] = 'DESC';
}


/**
 * @} End of "addtogroup hooks".
 */
