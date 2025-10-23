#######################################################################
# GCP EQUIVALENT OF AWS EMR "main.tf" — uses Google Cloud Dataproc
# Sections mirror your AWS file:
#  1) Providers
#  2) Variables
#  3) Storage (S3 -> GCS)
#  4) IAM / Service Account (EMR roles -> Dataproc SA + IAM bindings)
#  5) Dataproc Cluster (aws_emr_cluster -> google_dataproc_cluster)
#  6) (Optional) Job submission example (aws_emr_step -> google_dataproc_job)
#  7) Outputs (cluster id, connect hint)
#######################################################################

# -----------------------------------------------------------
# Terraform: Dataproc on Google Cloud (EMR equivalent) - FIXED
# -----------------------------------------------------------

terraform {
  required_version = ">= 1.5.0"

  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 5.40"
    }
    google-beta = {
      source  = "hashicorp/google-beta"
      version = "~> 5.40"
    }
  }
}

# -----------------------------------------------------------
# Providers
# -----------------------------------------------------------
provider "google" {
  project = var.project_id
  region  = var.region
  zone    = var.zone
}

provider "google-beta" {
  project = var.project_id
  region  = var.region
  zone    = var.zone
}

# -----------------------------------------------------------
# Variables
# -----------------------------------------------------------
variable "project_id" {
  description = "dev-splicer-475920-p9"
  type        = string
}

variable "region" {
  description = "GCP region (e.g., us-central1)"
  type        = string
  default     = "us-central1"
}

variable "zone" {
  description = "GCP zone (must align with region)"
  type        = string
  default     = "us-central1-a"
}

variable "cluster_name" {
  description = "Dataproc cluster name"
  type        = string
  default     = "kafka-cluster"
}

variable "bucket_name" {
  description = "GCS bucket for data/logs (globally unique)"
  type        = string
  default     = "kafka-bucket-2025"
}

variable "master_machine_type" {
  description = "Master node machine type"
  type        = string
  default     = "n2-standard-4"
}

variable "worker_machine_type" {
  description = "Worker node machine type"
  type        = string
  default     = "n2-standard-4"
}

variable "worker_count" {
  description = "Number of workers"
  type        = number
  default     = 2
}

variable "image_version" {
  description = "Dataproc image version (gcloud dataproc images list)"
  type        = string
  default     = "2.2-debian12"
}

# -----------------------------------------------------------
# Data sources (project number for service agent binding)
# -----------------------------------------------------------
data "google_project" "current" {}

# -----------------------------------------------------------
# Storage (S3 -> GCS)  -- location should be a valid region or multi-region
# -----------------------------------------------------------
resource "google_storage_bucket" "logs_data" {
  name          = var.bucket_name
  location      = var.region               # previously computed; keep simple & valid
  force_destroy = true

  uniform_bucket_level_access = true

  lifecycle_rule {
    condition { age = 30 }
    action    { type = "Delete" }
  }

  labels = {
    project = "kafka-data-pipeline"       # labels must be lowercase
  }
}

# -----------------------------------------------------------
# IAM / Service Account (EMR roles -> Dataproc SA + roles)
# -----------------------------------------------------------
resource "google_service_account" "dataproc_sa" {
  account_id   = "dataproc-sa"
  display_name = "Dataproc Cluster Service Account"
}

# Roles for cluster VM service account
resource "google_project_iam_member" "sa_roles" {
  for_each = toset([
    "roles/dataproc.editor",
    "roles/storage.admin",
    "roles/logging.logWriter",
    "roles/monitoring.metricWriter",
    "roles/dataproc.worker"               # REQUIRED for agents/tasks
  ])
  project = var.project_id
  role    = each.value
  member  = "serviceAccount:${google_service_account.dataproc_sa.email}"
}

# Make sure the Dataproc service agent has its role (normally auto-granted)
resource "google_project_iam_member" "dataproc_service_agent_role" {
  project = var.project_id
  role    = "roles/dataproc.serviceAgent"
  member  = "serviceAccount:service-${data.google_project.current.number}@dataproc-accounts.iam.gserviceaccount.com"
}

# -----------------------------------------------------------
# Dataproc Cluster (google_dataproc_cluster)
# -----------------------------------------------------------
resource "google_dataproc_cluster" "cluster" {
  name   = var.cluster_name
  region = var.region

  labels = {
    project = "kafka-data-pipeline"       # lowercase labels
  }

  cluster_config {
    staging_bucket = google_storage_bucket.logs_data.name

    gce_cluster_config {
      zone                   = var.zone
      tags                   = ["dataproc", "spark", "hadoop"]
      service_account        = google_service_account.dataproc_sa.email
      service_account_scopes = ["cloud-platform"]
      internal_ip_only       = false
    }

    master_config {
      num_instances = 1
      machine_type  = var.master_machine_type
      disk_config {
        boot_disk_type    = "pd-standard"
        boot_disk_size_gb = 100
      }
    }

    worker_config {
      num_instances = var.worker_count
      machine_type  = var.worker_machine_type
      disk_config {
        boot_disk_type    = "pd-standard"
        boot_disk_size_gb = 100
      }
    }

    software_config {
    image_version       = var.image_version

    # NOTE: ANACONDA has been removed (no longer supported on newer Dataproc images)
    # Keep JUPYTER if you want notebook support.
    optional_components = ["JUPYTER"]

    # If you need Conda-style environments, use an initialization action
    # such as pip-install.sh or custom Miniconda installer.

    override_properties = {
      "spark:spark.executor.memory"      = "4g"
      "spark:spark.driver.memory"        = "4g"
      "spark:spark.executor.instances"   = tostring(var.worker_count)
      "yarn:yarn.log-aggregation-enable" = "true"
    }
  }
    # Example init action if you need bootstrapping
    # initialization_action {
    #   script      = "gs://goog-dataproc-initialization-actions-us-central1/python/pip-install.sh"
    #   timeout_sec = 600
    #   args        = ["pyspark==3.5.0"]
    # }
  }

  graceful_decommission_timeout = "3600s"
}

# -----------------------------------------------------------
# Outputs
# -----------------------------------------------------------
output "dataproc_cluster_name" {
  value       = google_dataproc_cluster.cluster.name
  description = "Dataproc cluster name"
}

output "gcloud_master_ssh_hint" {
  value       = "gcloud compute ssh ${google_dataproc_cluster.cluster.name}-m --zone=${var.zone} --project=${var.project_id}"
  description = "SSH command for the master instance"
}
