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
 * no-op (boa-grav map par.1.3). Phase 2 (KahunaCart on MySQL) is then
 * "repoint db_server at a real MySQL node".
 */

class hostingService_db_none extends hostingService_db {
  public $type = 'none';
  public $name = 'None (flat-file sites)';
  public $has_port = FALSE;
  public $has_restart_cmd = FALSE;

  public function form(&$form) {
    parent::form($form);
    $form['note'] = array(
      '#type' => 'item',
      '#title' => t('No database'),
      '#markup' => t('This server provides no database service. Attach only flat-file sites (Grav capsules) to it; the backend maps it to a null service and every database operation becomes a no-op.'),
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
