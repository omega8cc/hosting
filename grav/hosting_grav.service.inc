<?php
/**
 * @file
 * The 'none' db service type: a real, selectable no-database server.
 *
 * DB-less Grav site capsules need a REAL db_server node (db_server = 0 emits
 * a visible hosting_context_name(0) error on every install and verify), but
 * hosting_get_servers('db') only offers nodes with an ENABLED db service and
 * the sole shipped class is hostingService_db_mysql. This class makes a
 * "no database" server selectable; its context writes the literal
 * db_service_type = 'NONE', which Provision_Context_server::spawn_service()
 * maps to Provision_Service_null — every backend db call becomes a silent
 * no-op. A Grav site on it keeps everything in files, or a KahunaCart store
 * in its own SQLite file; a shop with a MySQL database is created on a MySQL
 * server instead. The type's name is what the Servers list shows in the
 * Database column, so it says what a site on this server gets.
 */

class hostingService_db_none extends hostingService_db {
  public $type = 'none';
  public $name = 'Flat-file or SQLite';
  public $has_port = FALSE;
  public $has_restart_cmd = FALSE;

  public function form(&$form) {
    parent::form($form);
    $form['note'] = array(
      '#type' => 'item',
      '#title' => t('No MySQL database'),
      '#markup' => t('This server gives a site no MySQL database. Attach only Grav sites to it: a site that keeps everything in files, or a KahunaCart store kept in its own SQLite file (Verify warns about it, BOA does not run its worker, and Clone refuses the site). A shop belongs on a MySQL server. The backend maps this server to a null service and every database operation becomes a no-op.'),
    );
  }

  public function context_options($task_type, $ref_type, &$task) {
    parent::context_options($task_type, $ref_type, $task);
    // The literal spawn_service() sentinel — the parent wrote the lowercase
    // frontend type key, which would make provision look for a
    // Provision_Service_db_none class that deliberately does not exist.
    $task->context_options['db_service_type'] = 'NONE';
  }
}
