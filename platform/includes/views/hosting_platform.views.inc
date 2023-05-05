<?php
/**
 * @file
 * Hosting platform views integration.
 */

/**
 * Implements hook_views_data().
 */
function hosting_platform_views_data() {
  $data = [];
  $data['hosting_platform']['table'] = ['group' => 'Hosting Platform', 'title' => 'Platform', 'join' => ['node' => ['left_field' => 'vid', 'field' => 'vid']]];

  $data['hosting_platform']['web_server'] = ['title' => t('Web Server'), 'help' => t('Relate a platform to the web server it is hosted on.'), 'relationship' => ['handler' => 'views_handler_relationship', 'base' => 'node', 'field' => 'web_server', 'label' => t('web server')]];

  $data['hosting_platform']['publish_path'] = ['title' => t('Platform Path'), 'help' => t('The path on the server where this platform is installed.'), 'field' => ['handler' => 'views_handler_field_xss', 'click sortable' => TRUE]];

  $data['hosting_platform']['release'] = ['title' => t('Release'), 'help' => t('The release name.'), 'field' => ['field' => 'nid', 'handler' => 'hosting_platform_handler_field_release', 'click sortable' => TRUE]];

  $data['hosting_platform']['verified'] = ['title' => t('Verified Date'), 'help' => t('The most recent date that this platform was verified.'), 'field' => ['handler' => 'views_handler_field_date', 'click sortable' => TRUE]];

  $data['hosting_platform']['status'] = ['title' => t('Status'), 'help' => t('The current state of this platform.'), 'field' => ['handler' => 'hosting_platform_handler_field_status', 'click sortable' => TRUE], 'filter' => ['handler' => 'views_handler_filter_numeric']];

  $data['hosting_platform']['sites'] = ['title' => t('Sites'), 'help' => t('The number of sites in this platform.'), 'field' => ['handler' => 'hosting_platform_handler_field_sites', 'field' => 'nid']];

  return $data;
}
