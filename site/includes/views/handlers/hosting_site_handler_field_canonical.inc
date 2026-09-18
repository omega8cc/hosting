<?php
/**
 * @file Views handler for canonical URLs.
 */

class hosting_site_field_handler_canonical extends views_handler_field {
  function render($values) {
    $nid = $values->{$this->field_alias};
    $node = node_load($nid);
    // Use the redirection target, if it's available.
    if (is_object($node) && $node->type == 'site') {
      return hosting_site_canonical_url($node);
    }
    // Fall back to the task node title.
    if (isset($values->node_hosting_task_title)) return $values->node_hosting_task_title;
    // Default to the node title.
    if (isset($values->node_title)) return $values->node_title;
  }

  /**
   * Views breaks a trimmed domain on the word boundary just past a dot and
   * then appends its ellipsis, so the name would end in four dots.
   */
  function render_trim_text($alter, $value) {
    $trimmed = parent::render_trim_text($alter, $value);
    if ($trimmed !== $value && !empty($alter['ellipsis'])) {
      $trimmed = preg_replace('/\.+(' . preg_quote(t('...'), '/') . ')$/u', '$1', $trimmed);
    }
    return $trimmed;
  }
}

