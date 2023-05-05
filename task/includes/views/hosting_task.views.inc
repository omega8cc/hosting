<?php
/**
 * @file
 * Hosting task views integration.
 */

/**
 * Implements hook_views_data().
 */
function hosting_task_views_data() {
  $data = [];
  $data['hosting_task']['table'] = ['group' => 'Hosting Task', 'title' => 'Task', 'join' => ['node' => ['left_field' => 'vid', 'field' => 'vid'], 'node_revision' => ['left_field' => 'vid', 'field' => 'vid']]];

  $data['hosting_task']['task_type'] = ['title' => t('Type'), 'help' => t('The type of task.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE], 'filter' => ['handler' => 'hosting_task_handler_filter_type']];

  $data['hosting_task']['rid'] = ['title' => t('Reference ID'), 'help' => t('The referenced ID for the task.'), 'field' => ['handler' => 'views_handler_field_node', 'click sortable' => TRUE], 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('Reference')], 'argument' => ['handler' => 'views_handler_argument_node_nid']];

  $data['hosting_task']['task_status'] = ['title' => t('Status'), 'help' => t('Current status of the task.'), 'field' => ['handler' => 'hosting_task_handler_field_status', 'click sortable' => TRUE], 'filter' => ['handler' => 'hosting_task_handler_filter_status']];

  $data['hosting_task']['executed'] = ['title' => t('Executed'), 'help' => t('Date this task was executed.'), 'field' => ['handler' => 'views_handler_field_date', 'click sortable' => TRUE], 'sort' => ['handler' => 'views_handler_sort_date'], 'filter' => ['handler' => 'views_handler_filter_date'], 'argument' => ['handler' => 'views_handler_argument_date']];

  $data['hosting_task']['delta'] = ['title' => t('Time'), 'help' => t('Time this task took.'), 'field' => ['handler' => 'views_handler_field_time_interval', 'click sortable' => TRUE]];

  $data['hosting_task']['hosting_task_default'] = ['title' => t('Default'), 'help' => t('The default sort order for tasks.'), 'sort' => ['handler' => 'hosting_task_handler_sort_default']];

  return $data;
}
