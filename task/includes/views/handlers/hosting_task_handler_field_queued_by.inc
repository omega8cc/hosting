<?php
/**
 * @file
 * Views field handler for the login that queued a task.
 */

/**
 * Renders hosting_task.queued_by the way the task page does.
 *
 * @ingroup views_field_handlers
 */
class hosting_task_handler_field_queued_by extends views_handler_field {
  function construct() {
    parent::construct();
    $this->additional_fields['created'] = array('table' => 'node', 'field' => 'created');
  }

  function query() {
    $this->ensure_my_table();
    $this->add_additional_fields();
  }

  function render($values) {
    $created = isset($values->{$this->aliases['created']}) ? $values->{$this->aliases['created']} : 0;
    return hosting_task_queued_by_label($values->{$this->field_alias}, $created);
  }
}
