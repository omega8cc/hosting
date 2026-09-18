<?php
/**
 * @file
 * Views field handler for the login that queued a task.
 */

/**
 * Renders hosting_task.queued_by in the one-line form the task lists use.
 *
 * @ingroup views_field_handlers
 */
class hosting_task_handler_field_queued_by extends views_handler_field {
  function construct() {
    parent::construct();
    $this->additional_fields['created'] = array('table' => 'node', 'field' => 'created');
  }

  function render($values) {
    $created = 0;
    if (isset($this->aliases['created']) && isset($values->{$this->aliases['created']})) {
      $created = $values->{$this->aliases['created']};
    }
    $queued_by = isset($values->{$this->field_alias}) ? $values->{$this->field_alias} : '';
    return hosting_task_queued_by_short($queued_by, $created);
  }
}
