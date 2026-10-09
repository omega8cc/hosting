<?php
/**
 * @file Views handler for canonical URLs.
 */

class hosting_site_field_handler_canonical extends views_handler_field {

  /**
   * Redirection targets of the listed sites, keyed by site revision id.
   */
  var $redirections = array();

  function construct() {
    parent::construct();
    // Everything the canonical URL needs comes with the row: the site's
    // revision id, its title, and (fetched for all rows at once in
    // pre_render) its redirection target. Loading the node per row ran every
    // module's load hooks for every listed site.
    $this->additional_fields['vid'] = 'vid';
    $this->additional_fields['title'] = array('table' => 'node', 'field' => 'title');
  }

  function pre_render(&$values) {
    $this->redirections = array();
    if (!module_exists('hosting_alias') || empty($this->aliases['vid'])) {
      return;
    }
    $vids = array();
    foreach ($values as $row) {
      if (!empty($row->{$this->aliases['vid']})) {
        $vids[] = (int) $row->{$this->aliases['vid']};
      }
    }
    if (empty($vids)) {
      return;
    }
    $result = db_query("SELECT vid, redirection FROM {hosting_site_alias} WHERE vid IN (:vids)", array(':vids' => array_unique($vids)));
    foreach ($result as $row) {
      if (!isset($this->redirections[$row->vid]) && !empty($row->redirection)) {
        $this->redirections[$row->vid] = $row->redirection;
      }
    }
  }

  function render($values) {
    $nid = $values->{$this->field_alias};
    // A site row: use the redirection target, if it's available.
    if (!empty($nid)) {
      $vid = !empty($this->aliases['vid']) && isset($values->{$this->aliases['vid']}) ? $values->{$this->aliases['vid']} : NULL;
      $site = new stdClass();
      $site->title = !empty($this->aliases['title']) && isset($values->{$this->aliases['title']}) ? $values->{$this->aliases['title']} : '';
      $site->redirection = ($vid && isset($this->redirections[$vid])) ? $this->redirections[$vid] : '';
      return hosting_site_canonical_url($site);
    }
    // Fall back to the task node title.
    if (isset($values->node_hosting_task_title)) return $values->node_hosting_task_title;
    // Default to the node title.
    if (isset($values->node_title)) return $values->node_title;
  }

  /**
   * Views sets the link's title only when it differs from the link text, so
   * a name it did not trim carries none; the sites list's stylesheet can
   * still clip that name in the theme's fixed-width column, and the full
   * name must stay one hover away.
   */
  function render_as_link($alter, $text, $tokens) {
    if (!empty($alter['alt']) && !isset($alter['link_attributes']['title'])) {
      $alter['link_attributes']['title'] = $alter['alt'];
    }
    return parent::render_as_link($alter, $text, $tokens);
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
