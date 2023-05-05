<?php

/**
 * A handler for the Server status field.
 *
 * @ingroup views_field_handlers
 */
class hosting_server_handler_field_status extends hosting_field_handler_status {
  function render($values) {
    $value = $values->{$this->field_alias};
    $output = _hosting_server_status($value);
    return match ($this->options['status_mode']) {
        'image' => "<span class='hosting-status hosting-status-icon'></span>",
        'text_image' => "<span class='hosting-status'>{$output}</span>",
        'class' => _hosting_server_list_class($value),
        default => $output,
    };
  }
}
