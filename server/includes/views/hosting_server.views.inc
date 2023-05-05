<?php
/**
 * @file
 * Hosting server views integration.
 */

/**
 * Implements hook_views_data().
 */
function hosting_server_views_data() {
  $data = [];
  $data['hosting_server']['table'] = ['group' => 'Hosting Server', 'title' => 'Server', 'join' => ['node' => ['left_field' => 'vid', 'field' => 'vid']]];

  $data['hosting_server']['services'] = ['title' => t('Services'), 'help' => t('Available types of services per server.'), 'field' => ['handler' => 'hosting_server_handler_field_services', 'field' => 'nid']];

  $data['hosting_server']['human_name'] = ['title' => t('Human-readable name'), 'help' => t('A human-readable name for the server.'), 'field' => ['handler' => 'hosting_server_handler_field_human_name', 'click sortable' => TRUE]];

  $data['hosting_server']['verified'] = ['title' => t('Verified Date'), 'help' => t('The most recent date that this server was verified.'), 'field' => ['handler' => 'views_handler_field_date', 'click sortable' => TRUE]];

  $data['hosting_server']['status'] = ['title' => t('Status'), 'help' => t('The current state of this server.'), 'field' => ['handler' => 'hosting_server_handler_field_status', 'click sortable' => TRUE], 'filter' => ['handler' => 'views_handler_filter_numeric']];

  $data['hosting_service']['table'] = ['group' => 'Hosting Service', 'title' => 'Services', 'join' => ['node' => ['left_field' => 'vid', 'field' => 'vid']]];

  $data['hosting_service']['service'] = ['title' => t('Service'), 'help' => t('The type of service.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE]];

  $data['hosting_service']['type'] = ['title' => t('Type'), 'help' => t('The service sub-type.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE]];

  $data['hosting_service']['restart_cmd'] = ['title' => t('Restart command'), 'help' => t('The restart command for the service.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE]];

  $data['hosting_service']['port'] = ['title' => t('Port'), 'help' => t('The port for the service.'), 'field' => ['handler' => 'views_handler_field_numeric', 'click sortable' => TRUE]];

  $data['hosting_service']['available'] = ['title' => t('Available'), 'help' => t('Whether the service is available.'), 'field' => ['handler' => 'views_handler_field_boolean', 'click sortable' => TRUE]];

  return $data;
}
