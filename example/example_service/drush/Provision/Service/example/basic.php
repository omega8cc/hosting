<?php

 /**
  * @file
  *   A 'basic' implementation of the 'example' service type.
  */

/**
 * A class containing the 'basic' implementation of the 'example' service.
 *
 * This class is conditionally loaded when the "--example_service_type=basic"
 * option is passed to provision-save commands run on servers.
 *
 * The above flag is generated by the hosting counterpart of this class, which
 * provides the front end to configure all these fields.
 *
 * The responsibilities of this class include responding and saving any
 * values that are passed to it, and also to override the portions of
 * the public API for this service that are necessary.
 */
class Provision_Service_example_basic extends Provision_Service_example {
 /**
  * Some common options handled upstream by the base service classes.
  */

 /**
  *   This service needs to have a port specified for it.
  */
  public $has_port = TRUE;

 /**
  *   The default value for the port input.
  */
  function default_port() {
    return 12345;
  }

 /**
  *   This service needs to be restarted with a shell command.
  */
  public $has_restart_cmd = TRUE;

 /**
  *   The default value for the restart command input.
  */
  function default_restart_cmd() {
    return "/usr/bin/true";
  }


  /**
   * Initialize this class, including option handling.
   */
  function init_server() {
    // REMEMBER TO CALL THE PARENT!
    parent::init_server();

    /**
     * Register configuration classes for the create_config / delete_config methods.
     */
    $this->configs['server'][] = 'Provision_Config_Example';



    /**
     * Setting and storing a value.
     *
     * You will most commonly use :
     *    $this->server->setProperty('example_field', 'default');
     *
     * This helper will check for an existing saved value, overridden
     * by a command line option falling back to the default.
     *
     * This is the format used by everything you want configurable from
     * the front end or command line.
     *
     * These values will be saved in ~/.drush/server_name.drush.alias.inc.
     */
    $this->server->setProperty('example_field', 'default');


    /**
     * Non configurable values.
     *
     * If you want to generate values for use in your code or templates,
     * but don't want them to be overridden you would use the following format.
     *
     *   $this->server->example_config_path = $this->server->config_path . '/example.d'
     *
     * This will mean the value will change if you change the config path, but
     * you dont need to pass the right input to your command to get there,
     * and it's impossible to change the values.
     */
    $this->server->example_config_path = $this->server->config_path . '/example.d';
  }


  /**
   * Pass additional values to the config file templates.
   *
   * Even though the $server variable will be available in your template files,
   * you may wish to pass additional calculated values to your template files.
   *
   * Consider this something like the hook_preprocess stuff in drupal.
   */
  function config_data($config = null, $class = null) {
    // This format of calling the parent is very important!
    $data = parent::config_data($config, $class);

    /**
     * This value will become available as $example_current_time
     * in all the config files generated by this service.
     *
     * You could also choose to only conditionally pass values based on
     * the parameters.
     */
    $data['example_current_time'] = date(DATE_COOKIE, time());

    return $data;
  }

  /**
   * Implementation of service verify.
   */
  function verify() {
    parent::verify();
    if ($this->context->type == 'server') {
      // Create the configuration file directory.
      provision_file()->create_dir($this->server->example_config_path, dt("Example configuration"), 0700);
      // Sync the directory to the remote server if needed.
      $this->sync($this->server->example_config_path);
    }
  }
}