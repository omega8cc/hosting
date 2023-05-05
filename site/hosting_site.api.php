<?php

/**
 * @file
 * Hooks provided by the hosting site module.
 */

/**
 * @addtogroup hostinghooks
 * @{
 */

/**
 * This SSL key is a custom, Aegir generated one.
 */
define('HOSTING_SSL_CUSTOM_KEY', 'NULL');

/**
 * Alters which site options are available.
 *
 * @param $return
 *  An array of arrays, keys are fields on the $node and values are valid
 *  options for those fields.
 * @param $node
 *   The node object that represents the site.
 *
 * @see hosting_site_available_options()
 */
function hook_hosting_site_options_alter(&$return, $node) {
  // From: hosting_ssl_hosting_site_options_alter().

  // Disable the ssl key fields by default.
  if (!(is_countable(hosting_ssl_get_servers()) ? count(hosting_ssl_get_servers()) : 0)) {
    $return['ssl_enabled'] = FALSE;
  }

  $return['ssl_key'] = FALSE;
  $return['ssl_key_new'] = FALSE;

  // Test if ssl has been enabled.
  //if ($node->ssl_enabled != 0) {
  if (isset($node->ssl_enabled) && $node->ssl_enabled) {

    $keys = hosting_ssl_get_keys($node->client, TRUE);

    // return the list of valid keys, including the special 'new key' option.
    $return['ssl_key'] = array_keys($keys);

    // properly default this value so things dont fall apart later.
    if (count($return['ssl_key']) == 1) {
      $node->ssl_key = HOSTING_SSL_CUSTOM_KEY;
    }

    // the user has chosen to enter a new key
    if ($node->ssl_key == HOSTING_SSL_CUSTOM_KEY) {
      // default the new key to the site's domain name, after filtering.
      $default = hosting_ssl_filter_key($node->title);
      $return['ssl_key_new'] = (!empty($default)) ? $default : TRUE;
    }

    // we need to ensure that the return value is properly indexed, otherwise it
    // gets interpreted as an object by jquery.
    $return['profile'] = array_values(array_intersect($return['profile'], hosting_ssl_get_profiles()));

    $return['platform'] = array_values(array_intersect($return['platform'], hosting_ssl_get_platforms()));
  }
}

/**
 * Defines which filters are allowed to be used on the hosting site list.
 *
 * The list of sites can be filtered via the query string, and to avoid nasty
 * security exploits you must explicitly define what people can filter by.
 *
 * @return array
 *   An array of possible filter strings.
 *
 * @see hosting_site_get_possible_site_list_filters()
 * @see hosting_sites()
 * @see hook_hosting_site_site_list_filters_alter()
 */
function hook_hosting_site_site_list_filters() {
  // From hosting_platform.
  return ['platform'];
}

/**
 * Alters which filters are allowed to be used on the hosting site list.
 *
 * The list of sites can be filtered via the query string, and to avoid nasty
 * security exploits you must explicitly define what people can filter by.
 *
 * @param $filters
 *   The array of filters defined by other modules.
 *
 * @see hosting_site_get_possible_site_list_filters()
 * @see hosting_sites()
 * @see hook_hosting_site_site_list_filters()
 */
function hook_hosting_site_site_list_filters_alter(&$filters) {
  // Add a filter based on another module.
  if (in_array('other_filter', $filters, TRUE)) {
    $filters[] = 'my_filter';
  }
}

/**
 * @} End of "addtogroup hooks".
 */
