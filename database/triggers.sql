-- =============================================================================
-- Adaptive Cache DBMS Triggers
-- Audit logging and referential integrity automation
-- =============================================================================

USE adaptive_cache_db;

DELIMITER $$

-- -----------------------------------------------------------------------------
-- Trigger 1: trg_experiment_status_audit
-- Records an audit entry whenever an experiment's status transitions
-- -----------------------------------------------------------------------------
DROP TRIGGER IF EXISTS trg_experiment_status_audit$$
CREATE TRIGGER trg_experiment_status_audit
AFTER UPDATE ON workload_experiments
FOR EACH ROW
BEGIN
    IF OLD.status <> NEW.status THEN
        INSERT INTO experiment_audit_log (
            experiment_id, old_status, new_status, action_type, changed_at
        ) VALUES (
            NEW.experiment_id, OLD.status, NEW.status, 'STATUS_UPDATE', NOW()
        );
    END IF;
END$$

-- -----------------------------------------------------------------------------
-- Trigger 2: trg_experiment_create_audit
-- Records creation audit entry when a new experiment begins
-- -----------------------------------------------------------------------------
DROP TRIGGER IF EXISTS trg_experiment_create_audit$$
CREATE TRIGGER trg_experiment_create_audit
AFTER INSERT ON workload_experiments
FOR EACH ROW
BEGIN
    INSERT INTO experiment_audit_log (
        experiment_id, old_status, new_status, action_type, changed_at
    ) VALUES (
        NEW.experiment_id, NULL, NEW.status, 'EXPERIMENT_CREATED', NOW()
    );
END$$

DELIMITER ;
