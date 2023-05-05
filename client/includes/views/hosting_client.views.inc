<?php
/**
 * @file
 *   Client module views integration.
 */

/**
 * Implements hook_views_data().
 */
function hosting_client_views_data() {
  $data = [];
  $data['hosting_client'] = ['table' => ['group' => 'Hosting Client', 'title' => 'Client', 'join' => ['node' => ['left_field' => 'vid', 'field' => 'vid']]], 'uname' => ['title' => t('Internal name'), 'help' => t('The internal name for this client.'), 'field' => ['handler' => 'views_handler_field', 'click sortable' => TRUE]]];

  return $data;
}
