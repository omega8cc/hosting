<?php
/**
 * @file
 * Site node form.
 */

/**
 * Helper function to generate form elements for the site form.
 */
function _hosting_site_field(&$form, $node, $item, $element, $filter_display = 'filter_xss', $editable = FALSE, $show_desc = TRUE) {
  $css_id = str_replace("_", "-", $item);

  $type = $element['#type'];

  if (!isset($node->nid) || $editable) {
    // Create it.
    if (($element['#type'] == 'radios') && !count($element['#options'])) {
      $form[$item] = array(
        '#type' => 'value',
        '#value' => !empty($element['#default_value']) ? $element['#default_value'] : NULL,
      );
    }
    else {
      $form[$item] = $element;
    }

    if ($show_desc) {
      // The text to display when there are no valid options to select.
      $form[$item . '_description'] = array(
        '#prefix' => "<div class='hosting-site-field-description' id='hosting-site-field-{$css_id}-description'>",
        '#suffix' => '</div>',
        '#type' => 'item',
        '#title' => $element['#title'],
        '#description' => isset($element['#description']) ? $element['#description'] : NULL,
        '#markup' => "<div class='placeholder'>" . $filter_display($element['#default_value']) . "</div>",
      );
      if (isset($element['#weight'])) {
        $form[$item . '_description']['#weight'] = $element['#weight'];
      }
    }
  }
  else {
    $type = 'display';

    if ($show_desc) {
      // Display it.
      $form['info'][$item] = array(
        '#type' => 'item',
        '#title' => $element['#title'],
        '#markup' => !empty($element['#default_value'])? $filter_display($element['#default_value']): t('None'),
        '#required' => FALSE,
      );

      if (isset($element['#weight'])) {
        $form['info'][$item]['#weight'] = $element['#weight'];
      }
    }
    $form[$item] = array(
      '#type' => 'value',
      '#value' => isset($element['#default_value'])? $element['#default_value']: '',
    );
  }
  $form[$item]['#hosting_site_field'] = $item;
  $form[$item]['#hosting_site_field_value'] = $element['#default_value'];
  $form[$item]['#prefix'] = "<div class='hosting-site-field hosting-site-field-{$type}' id='hosting-site-field-{$css_id}'>";
  $form[$item]['#suffix'] = "</div>";
}


/**
 * Pass in a site node and return an array of valid options for it's fields.
 *
 * Modules can define the hook_hosting_site_options_alter function to modify
 * which fields are available for selection.
 */
function hosting_site_available_options($node, $platform = NULL) {
  // Cast to object if it's an array.
  $node = (is_array($node)) ? (object) $node : clone $node;

  $return = array();

  $return['platform'] = array();
  $return['site_language'] = array();

  if (!hosting_feature('client')) {
    // Setting the return value of a text field to null,
    // will signal to the front end that the field needs to
    // be displayed, but is not editable.
    $return['client'] = NULL;
  }

  // Load up the user we'll use to check platform and profile access.
  $user = user_load($GLOBALS['user']->uid);

  // Install profiles.
  if (!module_exists('hosting_package')) {
    return $return;
  }

  // Filter the available platforms based on which clients the user has access to.
  if (!is_null($platform)) {
    $return['platform'] = array($platform);
  }

  if (!isset($node->platform) || !in_array($node->platform, $return['platform'])) {
    $node->platform = $return['platform'][0];
  }

  // @TODO: Replace with something else.
  $return['site_language'] = array_keys((array) hosting_get_profile_languages());
  drupal_alter('hosting_site_options', $return, $node);

  return $return;
}


/**
 * Implements hook_form().
 */
function hosting_site_form($node, &$form_state) {
  $form['#node'] = $node;

  if (isset($node->nid)) {
    $form['info'] = array(
      '#prefix' => '<div class="clear-block" id="hosting-site-edit-info">',
      '#suffix' => '<br /></div>',
      '#weight' => -10,
    );
  }

  _hosting_site_field($form, $node, 'title', array(
    '#type' => 'textfield',
    '#title' => t('Domain name'),
    '#required' => TRUE,
    '#default_value' => isset($node->title) ? hosting_site_get_domain($node->title) : '',
    '#weight' => -20,
  ));

  // Settings
  $form['hosting_settings']['general'] = array(
    '#title' => t('Settings'),
    '#type' => 'fieldset',
    '#group' => 'site_settings',
    '#weight' => -100,
  );

  $form['hosting_settings']['general']['protected'] = array(
    '#title' => t('Protect Site'),
    '#description' => t('Prevent users from disabling, deleting or syncing to this site.'),
    '#weight' => -10,
    '#type' => 'checkbox',
    '#default_value' => $node->hosting_settings['general']['protected'],
  );

  $form['hosting_settings']['general']['allow_reinstall'] = array(
    '#title' => t('Allow Re-install'),
    '#description' => t('Give users a way to destroy and reinstall the site. '),
    '#weight' => -10,
    '#type' => 'checkbox',
    '#default_value' => $node->hosting_settings['general']['allow_reinstall'],
    '#states' => [
      'visible' => [
        ':input[name="hosting_settings[general][protected]"]' => ['checked' => FALSE],
      ],
    ],
  );

  $editable = ((!isset($node->client) || !isset($node->nid)) || user_access('administer sites')) && hosting_feature('client');
  $add_client_text = '';
  if (user_access('administer clients') || user_access('create client')) {
    $add_client_text = t('Click !here to add a new client.', array('!here' => l(t('here'), 'node/add/client', array('attributes' => array('target' => '_blank')))));
  }
  _hosting_site_field($form, $node, 'client', array(
    '#type' => 'textfield',
    '#required' => TRUE,
    '#title' => t('Client'),
    '#default_value' => _hosting_client_site_default($node),
    '#description' => t('The client to whom this site belongs.') . ' ' . $add_client_text,
    '#autocomplete_path' => 'hosting_client/autocomplete/client',
  ), 'filter_xss', $editable);

  // Override the defaults if the profile has been changed.
  if (isset($form_state['values']['platform'])) {
    $selected_platform = $form_state['values']['platform'];
  }
  else {
    $selected_platform = NULL;
  }

  _hosting_site_field($form, $node, 'site_language', array(
    '#type' => 'textfield',
    '#title' => t('Language'),
    '#description' => t("The language code to install on the site by default. You can change this later when configuring the site."),
    '#attributes' => array('class' => array("hosting-site-form-site-language-options")),
    '#default_value' => isset($node->site_language) ? $node->site_language : 'en',
  ), '_hosting_language_name');

  $platforms = _hosting_get_platforms(0, TRUE);

  _hosting_site_field($form, $node, 'platform', array(
    '#type' => 'radios',
    '#title' => t('Platform'),
    '#required' => TRUE,
    '#description' => t('The platform you want the site to be hosted on.'),
    '#options' => $platforms,
    '#default_value' => isset($node->platform) ? $node->platform : null,
  ), '_hosting_node_link');

  _hosting_site_field($form, $node, 'db_server', array(
    '#type' => 'radios',
    '#title' => t('Database server'),
    '#required' => TRUE,
    '#description' => t('The database server the site will use to host its content.'),
    '#options' => hosting_get_servers('db'),
    '#default_value' => isset($node->db_server) ? $node->db_server : HOSTING_DEFAULT_DB_SERVER,
  ), '_hosting_node_link');

  foreach (array('verified', 'last_cron', 'site_status') as $extra_attribute) {
    $form["$extra_attribute"] = array(
      '#type' => 'value',
      '#value' => isset($node->$extra_attribute) ? $node->$extra_attribute : NULL,
    );
  }

  // Files Paths
  $form['file_paths'] = array(
    '#title' => t('Site Files'),
    '#description' => t('The directory to use for Drupal site files. Paths may be relative to the Drupal index, or absolute on the server. <em>CAUTION:</em> The web server user will be granted write access permission to these folders so that the Drupal site can receive uploaded files. Do not change unless you are aware of the security implications.'),
    '#type' => 'fieldset',
    '#collapsed' => TRUE,
    '#collapsible' => TRUE,
    '#weight' => 10,
  );
  $form['file_paths']['file_public_path'] = array(
    '#title' => t('Public Files Path'),
    '#description' => t('Leave blank to use the default path: %path', array(
      "%path" => $node->title? "sites/{$node->title}/files": "sites/DOMAIN/files",
    )),
    '#type' => 'textfield',
    '#default_value' => $node->file_public_path,
  );
  $form['file_paths']['file_private_path'] = array(
    '#title' => t('Private Files Path'),
    '#description' => t('Leave blank to use the default path: %path', array(
      "%path" => $node->title? "sites/{$node->title}/files/private": "sites/DOMAIN/files/private",
    )),
    '#type' => 'textfield',
    '#default_value' => $node->file_private_path,
  );
  $form['file_paths']['file_temporary_path'] = array(
    '#title' => t('Temporary Files Path'),
    '#description' => t('Leave blank to use the default path: %path', array(
      "%path" => $node->title? "sites/{$node->title}/files/private/temp": "sites/DOMAIN/files/temp",
    )),
    '#type' => 'textfield',
    '#default_value' => $node->file_temporary_path,
  );

  // Hide files settings if not allowed.
  if (!variable_get('hosting_site_allow_custom_file_paths', FALSE)) {
    $form['file_paths']['#description'] = t('Drupal site file paths are not configurable. Check <em>Allow sites to have custom file paths</em> on the !settings page to allow customization.', array('!settings' => l(t('Hosting Settings'), 'admin/hosting/settings', array('query' => drupal_get_destination()))));

    foreach (element_children($form['file_paths']) as $i) {

      $form['file_paths']['display'][$i] = array(
        '#type' => 'item',
        '#title' => $form['file_paths'][$i]['#title'],
        '#markup' => $form['file_paths'][$i]['#default_value'],
      );
      $form['file_paths'][$i] = array(
        '#type' => 'value',
        '#value' => $form['file_paths'][$i]['#default_value'],
      );
    }
  }

  /** @TODO: Get platform settings into site node form.
  // Platform Settings
  $form['platform_node'] = array(
    '#title' => t('Platform Settings'),
    '#type' => 'fieldset',
    '#collapsed' => TRUE,
    '#collapsible' => TRUE,
  );

  // Load the form for this site's platform.
  $platform_node = node_load($node->platform);
  $platform_form = drupal_get_form('platform_node_form', $platform_node);

  // Clean up some things
  unset($platform_form['actions']);
  $platform_form['title']['#title'] = t('Platform Name');
  $platform_form['git']['#group'] = 'site_settings';

  // Tell hosting_site_node_update to skip adding the verify task. The site will include platform verification.
  $platform_form['no_verify'] = array(
    '#value' => TRUE,
  );
  _hosting_site_form_reset_parents_recursive($platform_form, 'platform_node');

  // Add all platform form elements on to site form.
  foreach (element_children($platform_form) as $element) {
    $form['platform_node'][$element] = $platform_form[$element];
  }
  */

  $form['site_settings'] = array(
    '#type' => 'vertical_tabs',
  );

  $form['hosting_settings']['deployment'] = array(
    '#title' => t('Deployment'),
    '#type' => 'fieldset',
    '#group' => 'site_settings',
  );
  $form['hosting_settings']['deployment']['methods'] = array(
  );
  $form['hosting_settings']['deployment']['methods']['continuous'] = [
    '#type' => 'checkbox',
    '#title' => t('Continous Deployment'),
    '#description' => t('When new code is available, deploy it as soon as possible. If not selected, you must deploy new code manually.'),
    '#default_value' => $node->hosting_settings['deployment']['methods']['continuous']
  ];
  $form['hosting_settings']['deployment']['methods']['manual_overrides'] = [
    '#type' => 'checkbox',
    '#title' => t('Allow overriding Deploy Steps'),
    '#description' => t('When starting a deployment manually, allow users to select the steps to run.'),
    '#default_value' => $node->hosting_settings['deployment']['methods']['manual_overrides'],
    '#states' => [
      'visible' => [
        ':input[name="hosting_settings[deployment][methods][manual]"]' => ['checked'   => TRUE],
      ]
    ]
  ];

  $form['hosting_settings']['deployment']['steps'] = hosting_site_deploy_steps_form_element($node);
  $form['hosting_settings']['deployment']['steps']['#title'] = t('Deploy Steps');
  $form['hosting_settings']['deployment']['steps']['#type'] = 'item';

  $form['hosting_settings']['deployment']['steps']['help'] = [
    '#theme' => 'item_list',
    '#items' => [
      t("The commands used for Deploy Steps can be overridden by adding a script named <code>deploy:STEP</code> to this site's composer.json file. See !link.", [
            '!link' => l(t('Composer Scripts Documentation'), 'https://getcomposer.org/doc/articles/scripts.md', ['absolute' => TRUE])
      ]),
      t('If <em>Allow overriding Manual Deployment Steps</em> is selected, these will be the default steps.'),
    ],
  ];
  // Hide the install form if editing a site that is protected.
  if ($node->nid) {
    $form['hosting_settings']['general']['install'] = hosting_site_install_methods_form_element($node);
    $form['hosting_settings']['general']['install']['#parents'] = ['hosting_settings', 'install'];

    $form['hosting_settings']['general']['install']['#states'] = [
      'visible' => [
        ':input[name="hosting_settings[general][protected]"]' => ['checked' => FALSE],
        ':input[name="hosting_settings[general][allow_reinstall]"]' => ['checked' => TRUE],
      ],
    ];
  }
  else {
    // On site create form, but install method on top.
    $form['hosting_settings']['install'] = hosting_site_install_methods_form_element($node);
    $form['hosting_settings']['install']['#title'] = t('Installation');
    $form['hosting_settings']['install']['#weight'] = -200;
    $form['hosting_settings']['install']['#type'] = 'fieldset';
    $form['hosting_settings']['install']['#group'] = 'site_settings';
  }
  return $form;
}

/**
 * Recursively reset form parents so embedded node forms work.
 */
function _hosting_site_form_reset_parents_recursive(&$element, $element_key, $parents = array()) {
  drupal_process_form();
  $parents[] = $element_key;
  $element['#processed'] = FALSE;
  $element['#id'] = FALSE;
  $element['#name'] = FALSE;
  $element['#parents'] = $parents;
  $element['#array_parents'] = $parents;
  $element['#parents_fixed'] = TRUE;
  foreach (element_children($element) as $element_child) {
    _hosting_site_form_reset_parents_recursive($element[$element_child], $element_child, $parents);
  }
}

/**
 * Ajax callback for the site profile field.
 */
function hosting_site_platform_callback($form, &$form_state) {
  return $form['platform'];
}

/**
 * Ajax callback for the site platform field.
 */
function hosting_site_language_callback($form, &$form_state) {
  return $form['site_language'];
}

/**
 * Implements hook_validate().
 */
function hosting_site_validate($node, &$form) {
  global $user;
  $valid_options = hosting_site_available_options($node);

  // Domain names are case-insensitive, set to lower case.
  $url = hosting_site_get_domain($node->title);

  if (!_hosting_valid_fqdn($url)) {
    form_set_error('title', t("You have not specified a valid url for this site."));
  }

  $length = strlen($url);
  if ($length > HOSTING_MAX_ALIAS_LENGTH) {
    $long = $length - HOSTING_MAX_ALIAS_LENGTH;
    form_set_error("title", t('The url your provided is @long character(s) too long. Please shorten.', array('@long' => $long)));
  }

  if (isset($node->new_client) && !$node->new_client) {
    $client = hosting_get_client($node->client);
    if (!$node->client || !$client) {
      form_set_error('client', t('Please fill in a valid client'));
    }
    if (!user_access('administer clients') && !array_key_exists($client->nid, hosting_get_client_from_user($user->uid))) {
      form_set_error('client', t('Access denied to client @client', array('@client' => $client->title)));
    }
    $node->client = $client->nid;
  }

  // TODO: maybe we should allow creation of sites that conflict with HOSTING_SITE_DISABLED (which would then need to be renamed before being re-enabled)
  if (!hosting_domain_allowed($url, (array) $node)) {
    form_set_error('title', t("The domain name you have specified is already in use."));
  }

  // If the quota module is loaded and this is a new node, check
  // the site quota.
  if (empty($node->nid) && function_exists('hosting_site_quota_exceeded')) {
    $quota_error = hosting_site_quota_exceeded($node);
    if ($quota_error) {
      form_set_error('title', $quota_error);
    }
  }
}

/**
 * Implements hook_form_alter().
 *
 * Hide the delete button on site nodes
 */
function hosting_site_form_alter(&$form, &$form_state, $form_id) {
  // Remove delete button from site edit form, unless the site's already been
  // deleted via the Delete task.
  if ($form_id == 'site_node_form') {
    $node = $form['#node'];
    if (isset($node->site_status) && $node->site_status !== HOSTING_SITE_DELETED) {
      $form['actions']['delete']['#type'] = 'hidden';
    }

    // Put all fieldgroups into vertical tabs.
    foreach (element_children($form) as $elementChildId) {
      if (isset($form[$elementChildId]['#type']) && $form[$elementChildId]['#type'] == 'fieldset') {
        $form[$elementChildId]['#group'] = 'site_settings';

        // Un-break aegir contrib.
        unset($form[$elementChildId]['#prefix']);
        unset($form[$elementChildId]['#suffix']);
      }
    }
  }
  if (array_key_exists('views_bulk_operations', $form)) {
    // Add our callback, so we can operate on the fully built form.
    $form['#after_build'][] = '_hosting_site_collapse_views_fieldset';
  }

  // On Delete task forms, if variable is set, set the --force option.
  if ($form_id == 'hosting_task_confirm_form' && $form['task']['#value'] == 'delete' && variable_get('hosting_delete_force', FALSE)) {
    $form['parameters']['force'] = array(
      '#type' => 'value',
      '#value' => 1,
    );
  }
}


/**
 * Renders a "Deploy Steps" form element.
 *
 * @TODO: Make this a real form element.
 * @param $node
 * @param array $parents
 * @return array
 * @throws Exception
 */
function hosting_site_deploy_steps_form_element($node = null, array $parents = []) {

  $node->platform_node = node_load($node->platform);

  $site = new Provision_Context_site($node->hosting_name);
  $platform = new Provision_Context_platform($node->platform_node->hosting_name);
  $site->setProperty('git_root', $node->git_root);
  $site->setProperty('platform', $platform);
  foreach ($site->getDeploySteps() as $step => $info) {

    if (!isset($info['command'])) {
      $info['command'] = [];
    }

    $info['command'] = is_string($info['command'])? array($info['command']): $info['command'];
    $element[$step] = [
      '#title' => t('@title: !note', [
          '@title' => $info['title'],
          '!note' => $info['description'],
        ]),
      '#description' => $info['note'] . (!empty($info['command'])? '<pre>' . implode(PHP_EOL, $info['command']) . '</pre>': ''),
      '#type' => 'checkbox',
      '#default_value' => $node->hosting_settings['deployment']['steps'][$step] ?? false,
    ];
    if ($parents) {
      $element[$step]['#parents'] = $parents;
      $element[$step]['#parents'][] = $step;
    }
  }
  return $element;
}

/**
 * Renders the "Install Methods" form element.
 *
 * @TODO: Make this a real form element.
 * @param $node
 * @param array $parents
 * @return array
 * @throws Exception
 */
function hosting_site_install_methods_form_element($node = null) {

  $values = $node->hosting_settings['install'];
  $parent = $node->hosting_settings_parent ?? 'hosting_settings';
  $element = array(
    '#type' => 'fieldset',
    '#tree' => TRUE,
    '#attributes' => array(
      'class' => array('clearfix'),
    ),
    '#id' => 'install-method-form'
  );
  $element['method'] = array(
    '#type' => 'radios',
    '#required' => TRUE,
    '#title' => t('Install Method'),
    '#process' => array('devshop_environment_method_process'),
    '#weight' => -99,
    '#options' => array(
      'profile' => t('Install Drupal'),
      'clone' => t('Clone Environment'),
      'manual' => t('Manual Install'),
      'import' => t('Import Database'),
    ),
    '#default_value' => $values['method'] ?? 'profile',
  );
  $element['none'] = array(
    '#type' => 'container',
    '#weight' => -98,
    '#states' => array(
      'visible' => array(
        ":input[name='{$parent}[install][method]']" => array('value' => 'manual'),
      ),
    ),
    'note' => array(
      '#markup' => t('The site code and database will be set up. Visit the site to proceed through the install process.'),
    ),
  );

  $element['profile'] = array(
    '#type' => 'machine_name',
    '#machine_name' => array(
      'exists' => 'always_false',
    ),
    '#title' => t('Drupal Install Profile'),
    '#required' => false,
    '#default_value' => $values['profile'] ?? $node->profile ?? '',
    '#title_display' => 'invisible',
    '#description' => t('(optional) Enter the exact name the install profile to use. It must already exist in your git branch or tag.'),
    '#element_validate' => array(
      'form_validate_machine_name'
    ),
    '#attributes' => array(
      'placeholder' => t('Profile Name'),
    ),
    '#weight' => -97,
    '#states' => array(
      'visible' => array(
       ":input[name='{$parent}[install][method]']" => array('value' => 'profile'),
      ),
    ),
  );

  // Generate environments options.
  $environment_options['@primary'] = t('Primary Environment');
  foreach ($project->environments as $e => $environment) {
    if ($e == 'NEW') {
      continue;
    }
    $link = l(' <i class="fa fa-external-link"></i> ' . $environment->uri,  $environment->url, array(
        'html' => TRUE,
        'attributes' => array(
          'target' => '_blank',
          'class' => array('btn-text'),
        ),
      )) . '</small>';
    $git_ref = $environment->git_ref;
    $icon = isset($environment->git_ref_type) && $environment->git_ref_type == 'tag'? 'tag': 'code-fork';
    $environment_options[$environment->drush_alias] = "<span>{$environment->name}</span> <small class='git-ref text-muted'><i class='fa fa-$icon'></i> $git_ref</small> <small>$link</small>";
  }

  // Add other aliases to the clone source.
  if (isset($project->settings->aliases)) {
    foreach ($project->settings->aliases as $alias => $data) {
      $link = l('<i class="fa fa-external-link"></i> ' . $data['uri'], 'http://' . $data['uri'], array(
          'html' => TRUE,
          'attributes' => array(
            'target' => '_blank',
            'class' => array('btn-text'),
          ),
        )) . '</small>';
      $remote = t('Unknown');
      $environment_options["@{$project->name}.{$alias}"] = "<span>{$alias}</span> <small class='git-ref text-muted'><i class='fa fa-globa'></i> $remote</small> <small>$link</small>";
    }
  }

  $environment_options['_other'] = t('Other Drush Alias');

  if (count($environment_options)) {
    $element['clone_source'] = array(
      '#type' => 'radios',
      '#title' => t('Environment to clone'),
      '#weight' => -98,
      '#options' => $environment_options,
      '#default_value' => $values['clone_source'] ?? '@primary',
      '#states' => array(
        'visible' => array(
         ":input[name='{$parent}[install][method]']" => array('value' => 'clone'),
        ),
      ),
    );
  }

  $element['clone_source_drush'] = array(
    '#type' => 'textfield',
    '#title' => t('Drush Alias'),
    '#default_value' => $values['clone_source_drush'] ?? '',
    '#description' => t('Enter a Drush alias you would like to Sync your database and files from. The alias can be a global drush alias file or in your site codebase at ./drush/sites/self.site.yml.'),
    '#element_validate' => array(
      'devshop_projects_create_environment_form_validate_clone_source'
    ),
    '#weight' => -98,
    '#attributes' => array(
      'placeholder' => t('@drush.alias'),
    ),
    '#states' => array(
      'visible' => array(
        ":input[name='{$parent}[install][clone_source]']" => array('value' => '_other'),
        ":input[name='{$parent}[install][method]']" => array('value' => 'clone'),
      ),
    ),
  );

  $element['import'] = array(
    '#type' => 'textfield',
    '#title' => t('Path to SQL'),
    '#default_value' => $values['import'] ?? '',
    '#description' =>
      t('Enter either a remote MySQL address (such as mysql://username:password@host/database), or an absolute path to an SQL dump (such as /var/aegir/site-backup.sql).') .
      '<span class="text-danger">' . t('This string is stored in plain text. Use with caution.') . '</span>'
  ,
    '#weight' => -98,
    '#states' => array(
      'visible' => array(
       ":input[name='{$parent}[install][method]']" => array('value' => 'import'),
      ),
    ),
    '#element_validate' => array(
      'devshop_projects_create_environment_form_validate_import'
    ),
  );

  $element['import_files'] = array(
    '#type' => 'textfield',
    '#title' => t('Path to Files Directory'),
    '#default_value' => $values['import_files'] ?? '',
    '#description' =>
      t('Enter an absolute path to a files directory (such as /var/aegir/backups/files-snapshot/).') .
      '<span class="text-danger">' . t('This string is stored in plain text. Use with caution.') . '</span>',
    '#weight' => -98,
    '#states' => array(
      'visible' => array(
        ":input[name='{$parent}[install][method]']" => array('value' => 'import'),
      ),
    ),
  );

  return $element;
}

/**
 * Confirmation form for site backups.
 *
 * @see hosting_task_confirm_form().
 */
function hosting_task_install_form($node) {

  drupal_set_title(t('Are you sure you want to reinstall the site?'));

  $form['install'] = hosting_site_install_methods_form_element($node);
  $form['install']['#parents'] = ['hosting_settings', 'install'];
  $form['install']['#element_validate'] = ['hosting_task_install_save_settings'];

  $form['force'] = array(
    '#title' => t('Destroy all data in this site'),
    '#type' => 'checkbox',
    '#required' => TRUE,
    '#description' => t('WARNING: Taking this action will destroy all data in the site !link.', array(
      '!link' => l($node->title, $node->title, array(
        'options' => array(
          'attributes' => array(
            'target' => '_blank',
          ),
        ),
      )),
    )),
  );

  return $form;
}


/**
 * Element validate hook for install task install settings.
 */
function hosting_task_install_save_settings($element, &$form_state) {
  if ($form_state['values']['parameters']['force']) {
    $site_node = $form_state['build_info']['args'][0];
    $site_node->hosting_settings['install'] = $form_state['values']['hosting_settings']['install'];
    $site_node->no_verify = TRUE;
    node_save($site_node);
  }
}
