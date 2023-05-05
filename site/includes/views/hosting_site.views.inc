<?php
/**
 * @file
 * Hosting site views integration.
 */

/**
 * Implements hook_views_data().
 */
function hosting_site_views_data() {
  $data = [];
  // Views data for 'hosting_site' table.
  $data['hosting_site']['table'] = ['group' => 'Hosting Site', 'title' => 'Site', 'join' => ['node' => ['left_field' => 'vid', 'field' => 'vid']]];

  $data['hosting_site']['client'] = ['title' => t('Client'), 'help' => t('Relate a node revision to the user who created the revision.'), 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('client')]];

  $data['hosting_site']['db_server'] = ['title' => t('Database Server'), 'help' => t('Database where the site is installed.'), 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('db server')]];

  $data['hosting_site']['profile'] = ['title' => t('Profile'), 'help' => t('Type of drupal site.'), 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('profile')]];

  $data['hosting_site']['platform'] = ['title' => t('Platform'), 'help' => t('Platform'), 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('platform')]];

  $data['hosting_site']['verified'] = ['title' => t('Verified'), 'help' => t('The last date verified task run on this site.'), 'field' => ['handler' => 'hosting_field_handler_interval', 'click sortable' => TRUE]];

  $data['hosting_site']['last_cron'] = ['title' => t('Last Cron Run'), 'help' => t('The time the last cron run was executed on this site.'), 'field' => ['handler' => 'hosting_field_handler_interval', 'click sortable' => TRUE]];

  $data['hosting_site']['language'] = ['title' => t('Language'), 'help' => t('The default language of this site.'), 'field' => ['handler' => 'hosting_site_handler_field_language', 'click sortable' => TRUE]];

  $data['hosting_site']['status'] = ['title' => t('Status'), 'help' => t('The current state of this site.'), 'field' => ['handler' => 'hosting_site_handler_field_status', 'click sortable' => TRUE], 'filter' => ['handler' => 'views_handler_filter_in_operator', 'options callback' => 'hosting_site_status_codes', 'options arguments' => ['label']], 'sort' => ['handler' => 'views_handler_sort']];

  // Views data for 'hosting_site_backups' table.
  $data['hosting_site_backups']['table'] = ['title' => 'Backups', 'group' => 'Hosting Site Backups', 'base' => ['title' => t('Hosting site backups'), 'help' => t('Hosting site backups'), 'field' => 'bid'], 'join' => ['node' => ['left_field' => 'nid', 'field' => 'site']]];

  $data['hosting_site_backups']['bid'] = ['title' => t('Bid'), 'help' => t('The site backup ID.'), 'field' => ['handler' => 'views_handler_field_numeric', 'click sortable' => TRUE], 'argument' => ['handler' => 'views_handler_argument_numeric', 'numeric' => TRUE], 'filter' => ['handler' => 'views_handler_filter_numeric'], 'sort' => ['handler' => 'views_handler_sort']];

  $data['hosting_site_backups']['site'] = ['title' => t('Nid'), 'help' => t('The backups site node ID.'), 'field' => ['handler' => 'views_handler_field_node', 'click sortable' => TRUE], 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('site')], 'argument' => ['handler' => 'views_handler_argument_node_nid']];

  $data['hosting_site_backups']['web_server'] = ['title' => t('Web Server'), 'help' => t('The web server the backup is hosted on.'), 'field' => ['handler' => 'views_handler_field_node', 'click sortable' => TRUE], 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'field' => 'web_server', 'label' => t('web server')], 'argument' => ['handler' => 'views_handler_argument_node_nid']];

  $data['hosting_site_backups']['description'] = ['title' => t('Description'), 'help' => t('The description of the backup.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE], 'sort' => ['handler' => 'views_handler_sort'], 'filter' => ['handler' => 'views_handler_filter_string'], 'argument' => ['handler' => 'views_handler_argument_string']];

  $data['hosting_site_backups']['filename'] = ['title' => t('File'), 'help' => t('The backup file.'), 'field' => ['handler' => 'views_handler_field_file', 'click sortable' => TRUE], 'sort' => ['handler' => 'views_handler_sort'], 'filter' => ['handler' => 'views_handler_filter_string'], 'argument' => ['handler' => 'views_handler_argument_string']];

  $data['hosting_site_backups']['size'] = ['title' => t('Size'), 'help' => t('The size of the backup file.'), 'field' => ['handler' => 'views_handler_field_file_size', 'click sortable' => TRUE], 'sort' => ['handler' => 'views_handler_sort'], 'filter' => ['handler' => 'views_handler_filter_numeric']];

  $data['hosting_site_backups']['timestamp'] = ['title' => t('Creation date'), 'help' => t('The date the backup was created.'), 'field' => ['handler' => 'views_handler_field_date', 'click sortable' => TRUE], 'sort' => ['handler' => 'views_handler_sort_date'], 'filter' => ['handler' => 'views_handler_filter_date']];

  $data['hosting_site']['canonical'] = [
    'title' => t('Canonical URL'),
    'help' => t('The canonical URL for this site.'),
    // This field value is based on the site NID.
    'real field' => 'nid',
    'field' => [
      'handler' => 'hosting_site_field_handler_canonical',
      'click sortable' => TRUE,
    ],
  ];

  return $data;
}

/**
 * Implements hook_views_pre_build().
 */
function hosting_site_views_pre_build(&$view) {
  if ($view->name == 'hosting_site_list') {
    switch ($view->current_display) {
      case 'page_sites':
        // Only allow VBO for admins.
        if (!user_access('administer sites')) {
          unset($view->field['views_bulk_operations']);
          unset($view->display_handler->handlers['field']['views_bulk_operations']);
        }
        break;
      case 'block_sites':
        // Rather than maintain separate views/displays for each hosting
        // content type, we alter the view to remove all but the relevant
        // contextual filters (arguments) here.
        // @see: hosting_site_block_view_alter().
        if($node = menu_get_object()) {
          if (in_array($node->type, ['platform', 'server', 'client', 'package'])) {
            foreach ($view->argument as $name => $arg) {
              if (!in_array($node->type, $arg->options['validate_options']['types'])) {
                unset($view->argument[$name]);
                unset($view->display_handler->handlers['argument'][$name]);
                unset($view->display_handler->display->display_options['arguments'][$name]);
                unset($view->display_handler->options['arguments'][$name]);
              }
            }
            // Only allow VBO for admins.
            if (!user_access('administer sites')) {
              unset($view->field['views_bulk_operations']);
              unset($view->display_handler->handlers['field']['views_bulk_operations']);
            }
          }
        }
        break;
    }
  }

}

/**
 * Implements hook_preprocess_views_view().
 */
function hosting_site_preprocess_views_view(&$vars) {
  $fieldset = [];
  if ($vars['view']->name == 'hosting_site_list') {
    // Wrap exposed filters in a fieldset.
    if ($vars['exposed']) {
      // Ensure required css and js libs are added
      drupal_add_js('misc/form.js');
      drupal_add_js('misc/collapse.js');

      // Assume other get vars are exposed filters, so expand fieldset
      // to show applied filters
      $class = ['collapsible', 'collapsed'];
      if (count($_GET) > 1){
        $class = ['collapsible'];
      }

      // Build fieldset element, using correct array nesting for theme_fieldset
      $fieldset['element'] = ['#title' => t('Filters'), '#attributes' => ['class' => $class], '#value' => $vars['exposed'], '#children' => ''];

      // Reassign exposed filter tpl var to fieldset value
      $vars['exposed'] = theme('fieldset', $fieldset);
    }
  }
}

/**
 * Callback to collapse VBO fieldset.
 *
 * @see: hosting_site_form_alter().
 */
function _hosting_site_collapse_views_fieldset($form, &$form_state) {
  // Ensure required js libs are added
  drupal_add_js('misc/form.js');
  drupal_add_js('misc/collapse.js');

  $form['select']['#attributes']['class'][] = ' collapsible collapsed';
  return $form;
}
