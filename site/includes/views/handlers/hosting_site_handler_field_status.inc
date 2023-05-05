<?php

/**
 * A handler for the Site status field.
 *
 * @ingroup views_field_handlers
 */
class hosting_site_handler_field_status extends hosting_field_handler_status {
  function render($values) {
    $value = $values->{$this->field_alias};
    $output = _hosting_site_status($value);
    return match ($this->options['status_mode']) {
        'image' => "<span class='hosting-status hosting-status-icon'></span>",
        'text_image' => "<span class='hosting-status'>{$output}</span>",
        'class' => _hosting_site_list_class($value, $values->{$this->aliases['verified']}),
        default => $output,
    };
  }
}
