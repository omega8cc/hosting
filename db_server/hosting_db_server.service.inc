<?php
/**
 * @file
 * Provide the hosting serivce classes for database integration.
 */

/**
 * The base db service type class.
 */
class hostingService_db extends hostingService {
  public $service = 'db';
}

/**
 * A MySQL specific db service implementation class.
 */
class hostingService_db_mysql extends hostingService_db {
  public $type = 'mysql';
  public $name = 'MySQL';
  public $has_port = TRUE;

  function default_port() {
    $ctrlf = '/data/conf/' . $this->db_user . '_use_proxysql.txt';
    if (is_file($ctrlf)) {
      return 6033;
    }
    else {
      return 3306;
    }
  }

  function form(&$form) {
    parent::form($form);
    $form['db_user'] = array(
      '#type' => 'textfield',
      '#required' => !empty($this->available),
      '#title' => t('Username'),
      '#description' => t('The user that will be used to create new mysql users and databases for new sites.'),
      '#size' => 40,
      '#default_value' => isset($this->db_user) ? $this->db_user : NULL,
      '#maxlength' => 64,
      '#weight' => 5,
    );
    $passwd_description = '';
    if (isset($this->db_passwd)) {
      $passwd_description = t('<strong>You have already set a password for this database server.</strong><br />');
    }
    $form['db_passwd'] = array(
      '#type' => 'password_confirm',
      '#required' => FALSE,
      '#description' => $passwd_description . t('The password for the user that will be used to create new mysql users and databases for new sites'),
      '#size' => 30,
      '#weight' => 10,
    );
  }

  function insert() {
    parent::insert();
    $this->save_db_credentials();
  }

  function update() {
    // Refresh the generic hosting_service revision row via the parent, then
    // upsert the credentials row separately. We intentionally do NOT use the
    // inherited delete-then-insert for the credentials (see save_db_credentials).
    parent::delete_revision();
    parent::insert();
    $this->save_db_credentials();
  }

  /**
   * Persist the database credentials for the current server revision.
   *
   * Written as an atomic upsert (db_merge) instead of the inherited
   * delete-then-insert: a failure part-way through — e.g. during a hostmaster
   * migrate — can no longer leave the row deleted. It also refuses to store
   * blank credentials, carrying the last known-good values forward instead.
   * Together these prevent the failure where an empty master_db wiped a
   * server's stored credentials, leaving every verify failing with
   * "Access denied for user ''@'localhost' (using password: NO)".
   */
  protected function save_db_credentials() {
    if (empty($this->db_user) || empty($this->db_passwd)) {
      $existing = db_query('SELECT db_user, db_passwd FROM {hosting_db_server} WHERE vid = :vid', array(':vid' => $this->server->vid))->fetchObject();
      if (empty($existing) || (empty($existing->db_user) && empty($existing->db_passwd))) {
        $existing = db_query_range('SELECT db_user, db_passwd FROM {hosting_db_server} WHERE nid = :nid ORDER BY vid DESC', 0, 1, array(':nid' => $this->server->nid))->fetchObject();
      }
      if (!empty($existing)) {
        if (empty($this->db_user) && !empty($existing->db_user)) {
          $this->db_user = $existing->db_user;
        }
        if (empty($this->db_passwd) && !empty($existing->db_passwd)) {
          $this->db_passwd = $existing->db_passwd;
        }
      }
    }

    db_merge('hosting_db_server')
      ->key(array('vid' => $this->server->vid))
      ->fields(array(
        'nid' => $this->server->nid,
        'db_user' => isset($this->db_user) ? $this->db_user : '',
        'db_passwd' => isset($this->db_passwd) ? $this->db_passwd : '',
      ))
      ->execute();
  }

  function delete_revision() {
    parent::delete_revision();
    db_delete('hosting_db_server')
      ->condition('vid', $this->server->vid)
      ->execute();
  }

  function delete() {
    parent::delete();
    db_delete('hosting_db_server')
      ->condition('nid', $this->server->nid)
      ->execute();
  }


  function load() {
    parent::load();
    $this->mergeData('SELECT db_user, db_passwd FROM {hosting_db_server} WHERE vid = :vid', array(':vid' => $this->server->vid));
  }

  function view(&$render) {
    parent::view($render);

    $render['db_user'] = array(
      '#type' => 'item',
      '#title' => t('Database user'),
      '#markup' => filter_xss($this->db_user),
      '#weight' => 1,
    );

    if (empty($this->db_passwd)) {
      $markup = t('None');
    }
    else {
      $markup = t('Saved');
    }
    $render['db_passwd'] = array(
        '#type' => 'item',
        '#title' => t('Database password'),
        '#markup' => $markup,
        '#weight' => 2,
    );
  }

  public function context_options($task_type, $ref_type, &$task) {
    parent::context_options($task_type, $ref_type, $task);

    // Provide context_options for verification and writing out to an alias
    $task->context_options['master_db'] = 'mysql' . '://' . urlencode($this->db_user) . ':' . urlencode($this->db_passwd) . '@' . $this->server->title;
  }

  public function context_import($context) {
    parent::context_import($context);

    $matches = array();
    preg_match("+^mysql://(.*):(.*)@.*$+", stripslashes($context->master_db), $matches);
    // Only adopt parsed credentials when they are non-empty: a degenerate
    // master_db (e.g. mysql://:@localhost) must never overwrite and wipe the
    // stored credentials.
    if (!empty($matches[1])) {
      $this->db_user = urldecode($matches[1]);
    }
    if (!empty($matches[2])) {
      $this->db_passwd = urldecode($matches[2]);
    }
  }
}
