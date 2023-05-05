<?php
/**
 * @file
 */

/**
 * @ingroup views_field_handlers
 */
class hosting_task_handler_field_status extends views_handler_field {
  function option_definition() {
    $options = parent::option_definition();

    $options['alter']['contains']['status_mode'] = ['default' => 'raw'];

    return $options;
  }

  function options_form(&$form, &$form_state) {
    parent::options_form($form, $form_state);

    if ($this->allow_advanced_render()) {
      $form['alter']['status_mode'] = ['#type' => 'radios', '#title' => t('Display mode'), '#options' => ['raw' => t('Raw value'), 'text' => t('Text value'), 'class' => t('CSS class')], '#default_value' => $this->options['alter']['status_mode'], '#description' => t('')];
    }
  }

  function render($values) {
    $value = $values->{$this->field_alias};
    return match ($this->options['alter']['status_mode']) {
        'text' => _hosting_parse_error_code($value),
        'class' => hosting_task_status_class($value),
        default => $value,
    };
  }
}
