<?php
/**
 * @file
 *   Hosting package views integration.
 */

/**
 * Implements hook_views_data().
 */
function hosting_package_views_data() {
  $data = [];
  // Hosting package.
  $data['hosting_package']['table'] = ['group' => 'Hosting Package', 'title' => 'Package', 'join' => ['node' => ['left_field' => 'vid', 'field' => 'vid']]];

  $data['hosting_package']['package_type'] = ['title' => t('Type'), 'help' => t('The package type.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE], 'filter' => ['handler' => 'views_handler_filter_in_operator', 'options callback' => '_hosting_package_types'], 'sort' => ['handler' => 'views_handler_sort'], 'argument' => ['handler' => 'views_handler_argument_string']];


  // Hosting package instance.
  $data['hosting_package_instance']['table']['group'] = t('Hosting Package Instance');

  $data['hosting_package_instance']['table']['base'] = ['field' => 'filename', 'title' => t('Hosting package instances'), 'help' => t('Hosting package instances')];

  $data['hosting_package_instance']['iid'] = ['title' => t('Instance ID'), 'help' => t('The package instance ID.'), 'field' => ['handler' => 'views_handler_field_numeric', 'click sortable' => TRUE], 'argument' => ['handler' => 'views_handler_argument_numeric', 'numeric' => TRUE], 'filter' => ['handler' => 'views_handler_filter_numeric'], 'sort' => ['handler' => 'views_handler_sort']];

  $data['hosting_package_instance']['rid'] = ['title' => t('Reference ID'), 'help' => t('The package instance reference ID.'), 'field' => ['handler' => 'views_handler_field_node', 'click sortable' => TRUE], 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('site')], 'argument' => ['handler' => 'views_handler_argument_node_nid']];

  $data['hosting_package_instance']['site_count'] = ['title' => t('Site count'), 'help' => t('A count of sites using a package.'), 'field' => ['field' => 'platform', 'handler' => 'hosting_package_handler_field_site_count', 'click sortable' => TRUE], 'sort' => ['handler' => 'hosting_package_handler_sort_site_count']];

  $data['hosting_package_instance']['package_id'] = ['title' => t('Package ID'), 'help' => t('The package ID.'), 'field' => ['handler' => 'views_handler_field_node', 'click sortable' => TRUE], 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'base field' => 'nid', 'label' => t('package')], 'argument' => ['handler' => 'views_handler_argument_node_nid']];

  $data['hosting_package_instance']['filename'] = ['title' => t('File'), 'help' => t('The package instance file.'), 'field' => ['handler' => 'views_handler_field_file', 'click sortable' => TRUE], 'sort' => ['handler' => 'views_handler_sort'], 'filter' => ['handler' => 'views_handler_filter_string'], 'argument' => ['handler' => 'views_handler_argument_string']];

  $data['hosting_package_instance']['schema_version'] = ['title' => t('Schema version'), 'help' => t('The package instance schema version.'), 'field' => ['handler' => 'views_handler_field_numeric', 'click sortable' => TRUE], 'argument' => ['handler' => 'views_handler_argument_numeric', 'numeric' => TRUE], 'filter' => ['handler' => 'views_handler_filter_numeric'], 'sort' => ['handler' => 'views_handler_sort']];

  $data['hosting_package_instance']['version'] = ['title' => t('Version'), 'help' => t('The package instance version.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE], 'argument' => ['handler' => 'views_handler_argument_string'], 'filter' => ['handler' => 'views_handler_filter_string'], 'sort' => ['handler' => 'views_handler_sort']];

  $data['hosting_package_instance']['version_code'] = ['title' => t('Version code'), 'help' => t('The package instance version code.'), 'field' => ['handler' => 'views_handler_field_numeric', 'click sortable' => TRUE], 'argument' => ['handler' => 'views_handler_argument_numeric', 'numeric' => TRUE], 'filter' => ['handler' => 'views_handler_filter_numeric'], 'sort' => ['handler' => 'views_handler_sort']];

  $data['hosting_package_instance']['status'] = ['title' => t('Status'), 'help' => t('The status of the package instance.'), 'field' => ['handler' => 'hosting_package_handler_field_status', 'click sortable' => TRUE], 'filter' => [
      'handler' => 'hosting_package_handler_filter_status',
      'label' => t('Status'),
      'type' => 'on-off',
      // use boolean_field = 1 instead of boolean_field <> 0 in WHERE statment
      'use equal' => TRUE,
  ], 'sort' => ['handler' => 'views_handler_sort']];

  return $data;
}


/**
 * Implements hook_views_plugins
 */
function hosting_package_views_plugins() {
  $plugins = ['access' => ['parent' => ['no ui' => TRUE, 'handler' => 'views_plugin_access', 'parent' => ''], 'package' => ['title' => t('Hosting package'), 'help' => t('Manage package access.'), 'handler' => 'hosting_package_plugin_access', 'path' => drupal_get_path('module', 'hosting_package') . '/includes/views']]];

  return $plugins;
}

/**
 * Views access callback.
 *
 * @param $type
 *   The display plugin.
 * @param $display
 *   The display ID (machine_name).
 * @param $account
 *   The current user.
 *
 * @return bool
 */
function hosting_package_views_access($type, $display, $account = NULL) {
  switch ($type) {
    case 'page':
      $path = explode('/', (string) drupal_get_normal_path($_GET['q']));
      $nid = $path['1'];
      if (is_numeric($nid)) {
        $node = node_load($nid);
        if (!is_null($account) && (!user_access('view package', $account) || !node_access('view', $node, $account))) {
          return FALSE;
        }
        switch ($display) {
          case 'page_packages_site':
            return $node->type == 'site' && $node->site_status != -2;
            break;
          case 'page_packages_platform':
            return $node->type == 'platform' && $node->platform_status != -2;
            break;
        }
      }
      break;
    case 'block':
      if ($node = menu_get_object()) {
        return $node->type == 'package' && $node->package_type != 'profile';
      }
      break;
    default:
      return FALSE;
  }

  return FALSE;
}

/**
 * Implements hook_form_FORM_ID_alter().
 */
function hosting_package_form_views_exposed_form_alter(&$form, &$form_state) {
  // The status filter doesn't make sense on platforms.
  if ($form_state['view']->name == 'hosting_package_list' && is_numeric(arg(1)) && $node = node_load(arg(1))) {
    if ($node->type == 'platform') {
      $form['status']['#access'] = FALSE;
      unset($form['#info']['filter-status']);
    }
  }
}

/**
 * Implements hook_views_pre_build().
 */
function hosting_package_views_pre_build(&$view) {
  // The site_count field doesn't make sense for sites.
  if ($view->name == 'hosting_package_list' && is_numeric(arg(1)) && $node = node_load(arg(1))) {
    if ($node->type == 'site') {
      unset($view->field['site_count']);
      unset($view->display_handler->handlers['field']['site_count']);
    }
  }
}
