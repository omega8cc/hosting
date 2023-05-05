<?php

/**
 * A handler for the Package status field.
 *
 * @ingroup views_field_handlers
 */
class hosting_package_handler_field_status extends hosting_site_handler_field_status {

  /**
   * Constructor; calls to base object constructor.
   */
  function construct() {
    views_handler_field::construct();
  }

  function render($values) {
    $value = $values->{$this->field_alias};
    $output = $value ? t('Enabled') : t('Disabled');
    return match ($this->options['status_mode']) {
        'image' => "<span class='hosting-status hosting-status-icon'></span>",
        'text_image' => "<span class='hosting-status'>{$output}</span>",
        'class' => $value ? 'hosting-success' : 'hosting-info',
        default => $output,
    };
  }
}
